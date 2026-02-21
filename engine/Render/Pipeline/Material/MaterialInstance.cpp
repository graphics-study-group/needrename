#include "MaterialInstance.h"

#include "Asset/Material/MaterialAsset.h"
#include "Render/Memory/StructuredBufferPlacer.h"
#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"
#include "Render/Memory/ShaderParameters/ShaderResourceBinding.h"
#include "Render/Pipeline/PipelineInfo.h"
#include "Render/Pipeline/PipelineUtils.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Render/Renderer/VertexAttribute.h"
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Texture/Image2DTextureAsset.h>
#include <Asset/Texture/ImageCubemapAsset.h>
#include <Asset/Texture/SolidColorTextureAsset.h>
#include <Render/Memory/IndexedBuffer.h>
#include <SDL3/SDL.h>
#include <bitset>
#include <gtc/type_ptr.hpp>

namespace Engine {

    struct MaterialInstance::impl {
        struct PassInfo {
            // As UBOs and descriptor sets are GPU stuff, they only require two copies
            // (i.e. one for the frame being drawn, another to be updated by CPU). However
            // directly using the frame-in-flight index saves a lot of headache so we will
            // directly use this index.
            static constexpr uint32_t BACK_BUFFERS = 3;

            std::unordered_map <uint32_t, std::string> ubo_name_lut{};
            std::unordered_map <uint32_t, std::unique_ptr<IndexedBuffer>> ubos{};

            std::array <vk::DescriptorSet, BACK_BUFFERS> desc_set_cache{};

            std::bitset<8> _is_ubo_dirty{};
        };

        std::unique_ptr <ShaderResourceBinding> p_srb{};
        std::unique_ptr <StructuredBuffer> p_buffer{};
        std::unordered_map <const MaterialTemplate *, PassInfo> m_pass_infos{};

        // A small buffer for uniform buffer staging to avoid random write to UBO.
        std::vector<std::byte> m_buffer{};

        std::unordered_map <std::string,
            std::variant<
                std::shared_ptr <const Texture>,
                std::shared_ptr <const DeviceBuffer>
            >
        > owned_resources;

        void SetUboDirtyFlags() noexcept {
            for (auto & [k, v] : m_pass_infos) {
                v._is_ubo_dirty.set();
            }
        }

        auto CreatePassInfo (RenderSystem & system, MaterialTemplate & tpl) 
            -> std::unordered_map <const MaterialTemplate *, PassInfo>::iterator {
            assert(!m_pass_infos.contains(&tpl));

            using PassInfo = impl::PassInfo;
            // Allocate uniform buffers and per-material descriptor sets
            PassInfo pass{};
            const auto & splayout = tpl.GetReflectedShaderInfo();

            for (const auto & pinterface : splayout.interfaces) {
                if (auto pbuffer = dynamic_cast <const ShdrRfl::SPInterfaceBuffer *>(pinterface.get())) {
                    if (pbuffer->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer) {
                        auto psb = dynamic_cast<const ShdrRfl::SPInterfaceStructuredBuffer *>(pbuffer);
                        if (!psb) {
                            SDL_LogWarn(
                                SDL_LOG_CATEGORY_RENDER,
                                "Uniform buffer named %s is not structured, and cannot be manipulated.",
                                pbuffer->name.c_str()
                            );
                            continue;
                        }

                        pass.ubos[pbuffer->layout_binding] = IndexedBuffer::CreateUnique(
                            system.GetAllocatorState(),
                            {BufferTypeBits::HostAccessibleUniform},
                            psb->buffer_placer->CalculateMaxSize(),
                            system.GetDeviceInterface().QueryLimit(
                                RenderSystemState::DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment
                            ),
                            PassInfo::BACK_BUFFERS,
                            std::format(
                                "Indexed UBO {} for Material",
                                pbuffer->name
                            )
                        );
                        pass.ubo_name_lut[pbuffer->layout_binding] = pbuffer->name;
                    }
                }
            }
            pass._is_ubo_dirty.set();
            
            m_pass_infos[&tpl] = std::move(pass);
            return m_pass_infos.find(&tpl);
        }
    };

    MaterialInstance::MaterialInstance(RenderSystem &system,
            MaterialLibrary &library) :
        m_system(system), m_library(library), pimpl(std::make_unique<impl>()) {
        pimpl->p_srb = std::make_unique<ShaderResourceBinding>(m_system.GetIRCache());
        pimpl->p_buffer = std::make_unique<StructuredBuffer>();
    }

    MaterialInstance::~MaterialInstance() = default;

    void MaterialInstance::AssignScalarVariable(
        const std::string &name,
        std::variant<uint32_t, float> value
    ) {
        this->pimpl->SetUboDirtyFlags();
        struct Visitor {
            impl* pimpl;
            const std::string & name;

            void operator () (uint32_t v) {
                pimpl->p_buffer->SetVariable<uint32_t>(name, v);
            };
            void operator () (float v) {
                pimpl->p_buffer->SetVariable<float>(name, v);
            };
        };

        std::visit(Visitor{pimpl.get(), name}, value);
    }

    void MaterialInstance::AssignVectorVariable(const std::string &name, std::variant<glm::vec4, glm::mat4> value) {
        this->pimpl->SetUboDirtyFlags();

        struct Visitor {
            impl* pimpl;
            const std::string & name;

            void operator () (const glm::vec4 & v) {
                pimpl->p_buffer->SetVariable<const float, 4>(
                    name,
                    std::span<const float, 4>(glm::value_ptr(v), glm::value_ptr(v) + 4)
                );
            };
            void operator () (const glm::mat4 & v) {
                pimpl->p_buffer->SetVariable<const float, 16>(
                    name,
                    std::span<const float, 16>(glm::value_ptr(v), glm::value_ptr(v) + 16)
                );
            };
        };
        std::visit(Visitor{pimpl.get(), name}, value);
    }

    void MaterialInstance::AssignTexture(const std::string &name, std::shared_ptr <const Texture> texture) {
        this->pimpl->owned_resources[name] = texture;
        this->pimpl->p_srb->BindTexture(name, *texture);
    }

    void MaterialInstance::AssignBuffer(const std::string &name, std::shared_ptr <const DeviceBuffer> buffer) {
        this->pimpl->owned_resources[name] = buffer;
        this->pimpl->p_srb->BindBuffer(name, *buffer);
    }

    std::vector <uint32_t> MaterialInstance::UpdateGPUInfo(MaterialTemplate & tpl, uint32_t backbuffer) {
        assert(backbuffer < impl::PassInfo::BACK_BUFFERS);

        auto itr = pimpl->m_pass_infos.find(&tpl);
        if (itr == pimpl->m_pass_infos.end()) {
            SDL_LogVerbose(
                SDL_LOG_CATEGORY_RENDER,
                "Lazily allocating descriptor and UBOs for material template %p.",
                static_cast<const void *>(&tpl)
            );
            itr = pimpl->CreatePassInfo(m_system, tpl);
        }
        auto & pass_info = itr->second;

        // First prepare descriptor writes
        std::vector <uint32_t> dynamic_offsets;
        for (const auto & [k, v] : pass_info.ubos) {
            pimpl->p_srb->BindBuffer(
                pass_info.ubo_name_lut[k],
                *v,
                0,
                v->GetSliceSize()
            );
            // FIXME: Dynamic offset order might not be correct.
            dynamic_offsets.push_back(v->GetSliceOffset(backbuffer));
        }

        pass_info.desc_set_cache[backbuffer] = pimpl->p_srb->GetDescriptorSet(
            2,
            tpl.GetReflectedShaderInfo(),
            m_system.GetDevice(),
            tpl.GetDescriptorPool(),
            true, false
        );

        // Then do UBO buffer writes
        if (pass_info._is_ubo_dirty[backbuffer]) {
            const auto & splayout = tpl.GetReflectedShaderInfo();

            for (const auto & [k, v] : pass_info.ubos) {
                auto itr = splayout.interface_name_mapping.find(
                    pass_info.ubo_name_lut[k]
                );
                assert(itr != splayout.interface_name_mapping.end());
                auto pbuf = dynamic_cast<const ShdrRfl::SPInterfaceStructuredBuffer *>(itr->second);
                assert(pbuf && pbuf->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer);

                splayout.PlaceBufferVariable(
                    pimpl->m_buffer,
                    *pbuf,
                    *pimpl->p_buffer
                );

                std::memcpy(
                    v->GetSlicePtr(backbuffer),
                    this->pimpl->m_buffer.data(),
                    this->pimpl->m_buffer.size()
                );
            }
            
            pass_info._is_ubo_dirty[backbuffer] = false;
        }

        return dynamic_offsets;
    }

    std::vector <uint32_t> MaterialInstance::UpdateGPUInfo(
        const std::string &tag, VertexAttribute type, uint32_t backbuffer
    ) {
        auto tpl = GetLibrary().FindMaterialTemplate(tag, type);
        assert(tpl);
        return this->UpdateGPUInfo(*tpl, backbuffer);
    }

    vk::DescriptorSet MaterialInstance::GetDescriptor(const MaterialTemplate &tpl, uint32_t backbuffer) const noexcept {
        auto itr = pimpl->m_pass_infos.find(&tpl);
        if (itr == pimpl->m_pass_infos.end())   return nullptr;
        return itr->second.desc_set_cache[backbuffer];
    }

    vk::DescriptorSet MaterialInstance::GetDescriptor(
        const std::string &tag, VertexAttribute type, uint32_t backbuffer
    ) const noexcept {
        assert(backbuffer < impl::PassInfo::BACK_BUFFERS);

        auto tpl = GetLibrary().FindMaterialTemplate(tag, type);
        assert(tpl);
        return this->GetDescriptor(*tpl, backbuffer);
    }
    void MaterialInstance::Instantiate(const MaterialAsset &asset) {
        for (const auto & prop : asset.m_properties) {
            auto p = prop.second;
            switch(p.m_type) {
            case MaterialProperty::Type::UBO:
                break;
            case MaterialProperty::Type::SSBO:
                break;
            case MaterialProperty::Type::StorageImage:
                break;
            case MaterialProperty::Type::Texture:
            {
                auto texture_asset =
                    std::any_cast<AssetRef>(p.m_value).as<Image2DTextureAsset>();
                if (texture_asset) {
                    // TODO: We should allocate texture from assets in a pool.
                    auto texture = std::shared_ptr<ImageTexture>(std::move(ImageTexture::CreateUnique(this->m_system, *texture_asset)));
                    AssignTexture(prop.first, texture);
                    m_system.GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
                        *texture, texture_asset->GetPixelData(), texture_asset->GetPixelDataSize()
                    );
                }
                auto solid_color_asset =
                    std::any_cast<AssetRef>(p.m_value).as<SolidColorTextureAsset>();
                if (solid_color_asset) {
                    std::shared_ptr texture = ImageTexture::CreateUnique(
                        this->m_system, 
                        ImageTexture::ImageTextureDesc{
                            .dimensions = 2,
                            .width = 4,
                            .height = 4,
                            .depth = 1,
                            .mipmap_levels = 1,
                            .array_layers = 1,
                            .format = ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm,
                            .is_cube_map = false
                        }, 
                        Texture::SamplerDesc{}, 
                        "Sampled Albedo"
                    );
                    AssignTexture(prop.first, texture);
                    m_system.GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(
                        *texture,
                        { solid_color_asset->m_color.r, solid_color_asset->m_color.g, solid_color_asset->m_color.b, solid_color_asset->m_color.a }
                    );
                }
                break;
            }
            case MaterialProperty::Type::CubeTexture:
            {
                auto texture_asset =
                    std::any_cast<AssetRef>(p.m_value).as<ImageCubemapAsset>();
                auto texture = std::shared_ptr<ImageTexture>(std::move(ImageTexture::CreateUnique(this->m_system, *texture_asset)));
                AssignTexture(prop.first, texture);
                m_system.GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
                    *texture, texture_asset->GetPixelData(), texture_asset->GetPixelDataSize()
                );
                break;
            }
            case MaterialProperty::Type::Simple:
                switch(p.m_ubo_type) {
                case MaterialProperty::InBlockVarType::Float:
                    AssignScalarVariable("Material::" + prop.first, std::any_cast<float>(p.m_value));
                    break;
                case MaterialProperty::InBlockVarType::Int:
                    AssignScalarVariable("Material::" + prop.first, std::any_cast<uint32_t>(p.m_value));
                    break;
                case MaterialProperty::InBlockVarType::Vec4:
                    AssignVectorVariable("Material::" + prop.first, std::any_cast<glm::vec4>(p.m_value));
                    break;
                case MaterialProperty::InBlockVarType::Mat4:
                    AssignVectorVariable("Material::" + prop.first, std::any_cast<glm::mat4>(p.m_value));
                    break;
                default:
                    ;
                }
                break;
            default:
                SDL_LogError(
                    SDL_LOG_CATEGORY_RENDER,
                    "Unidentified material property %s.",
                    prop.first.c_str()
                );
            }
        }
    }
    MaterialLibrary & MaterialInstance::GetLibrary() const {
        return m_library;
    }
} // namespace Engine
