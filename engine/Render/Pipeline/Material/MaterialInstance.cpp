#include "MaterialInstance.h"

#include "Asset/Material/MaterialAsset.h"
#include "Render/Memory/ShaderParameters/ShaderParameter.h"
#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"
#include "Render/Pipeline/PipelineInfo.h"
#include "Render/Pipeline/PipelineUtils.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Texture/Image2DTextureAsset.h>
#include <Render/Memory/IndexedBuffer.h>
#include <SDL3/SDL.h>
#include <bitset>

namespace Engine {

    struct MaterialInstance::impl {
        struct PassInfo {
            // As UBOs and descriptor sets are GPU stuff, they only require two copies
            // (i.e. one for the frame being drawn, another to be updated by CPU). However
            // directly using the frame-in-flight index saves a lot of headache so we will
            // directly use this index.
            static constexpr uint32_t BACK_BUFFERS = 3;

            std::unordered_map <std::string, std::unique_ptr<IndexedBuffer>> ubos{};

            std::bitset<8> _is_ubo_dirty{};
            std::bitset<8> _is_descriptor_dirty{};

            std::array <vk::DescriptorSet, BACK_BUFFERS> desc_sets{};
            
        };

        std::weak_ptr <MaterialLibrary> m_library;

        ShdrRfl::ShaderParameters parameters{};
        std::unordered_map <const MaterialTemplate *, PassInfo> m_pass_infos{};

        // A small buffer for uniform buffer staging to avoid random write to UBO.
        std::vector<std::byte> m_buffer{};

        void SetUboDirtyFlags() noexcept {
            for (auto & [k, v] : m_pass_infos) {
                v._is_ubo_dirty.set();
            }
        }

        void SetDescriptorDirtyFlags() noexcept {
            for (auto & [k, v] : m_pass_infos) {
                v._is_descriptor_dirty.set();
            }
        }

        auto CreatePassInfo (RenderSystem & system, MaterialTemplate & tpl) 
            -> std::unordered_map <const MaterialTemplate *, PassInfo>::iterator {
            assert(!m_pass_infos.contains(&tpl));

            using PassInfo = impl::PassInfo;
            // Allocate uniform buffers and per-material descriptor sets
            PassInfo pass{};
            const auto & splayout = tpl.GetReflectedShaderInfo();
            
            auto allocated_sets = tpl.AllocateDescriptorSets(impl::PassInfo::BACK_BUFFERS);
            assert(allocated_sets.size() == pass.desc_sets.size());
            std::copy(allocated_sets.begin(), allocated_sets.end(), pass.desc_sets.begin());

            for (auto pinterface : splayout.interfaces) {
                if (auto pbuffer = dynamic_cast <const ShdrRfl::SPInterfaceBuffer *>(pinterface)) {
                    if (pbuffer->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer) {
                        auto ptype = dynamic_cast <const ShdrRfl::SPTypeSimpleStruct *>(pbuffer->underlying_type);
                        assert(ptype);

                        pass.ubos[pbuffer->name] = IndexedBuffer::CreateUnique(
                            system,
                            Buffer::BufferType::Uniform,
                            ptype->expected_size,
                            system.GetPhysicalDevice().getProperties().limits.minUniformBufferOffsetAlignment,
                            PassInfo::BACK_BUFFERS,
                            std::format(
                                "Indexed UBO {} for Material",
                                pbuffer->name
                            )
                        );
                    }
                }
            }

            pass._is_descriptor_dirty.set();
            pass._is_ubo_dirty.set();
            
            m_pass_infos[&tpl] = std::move(pass);
            return m_pass_infos.find(&tpl);
        }
    };

    MaterialInstance::MaterialInstance(RenderSystem &system,
            std::shared_ptr <MaterialLibrary> library) :
        m_system(system), pimpl(std::make_unique<impl>(impl{library})) {
    }

    MaterialInstance::~MaterialInstance() = default;

    void MaterialInstance::AssignScalarVariable(
        const std::string &name,
        std::variant<uint32_t, int32_t, float> value
    ) {
        this->pimpl->SetUboDirtyFlags();
        this->pimpl->parameters.Assign(name, value);
    }

    void MaterialInstance::AssignVectorVariable(const std::string &name, std::variant<glm::vec4, glm::mat4> value) {
        this->pimpl->SetUboDirtyFlags();
        this->pimpl->parameters.Assign(name, value);
    }

    void MaterialInstance::AssignTexture(const std::string &name, const Texture &texture) {
        this->pimpl->SetDescriptorDirtyFlags();
        this->pimpl->parameters.Assign(name, texture);
    }

    void MaterialInstance::AssignBuffer(const std::string &name, const Buffer &buffer) {
        this->pimpl->SetDescriptorDirtyFlags();
        this->pimpl->parameters.Assign(name, buffer);
    }

    const ShdrRfl::ShaderParameters &MaterialInstance::GetShaderParameters() const noexcept {
        return pimpl->parameters;
    }

    void MaterialInstance::UpdateGPUInfo(MaterialTemplate * tpl, uint32_t backbuffer) {
        assert(backbuffer < impl::PassInfo::BACK_BUFFERS);
        assert(tpl);

        auto itr = pimpl->m_pass_infos.find(tpl);
        if (itr == pimpl->m_pass_infos.end()) {
            SDL_LogVerbose(
                SDL_LOG_CATEGORY_RENDER,
                "Lazily allocating descriptor and UBOs for material template %p.",
                (const void *)&tpl
            );
            itr = pimpl->CreatePassInfo(m_system, *tpl);
        }
        auto & pass_info = itr->second;

        // First prepare descriptor writes
        if (pass_info._is_descriptor_dirty[backbuffer]) {
            // Point UBOs to internal buffers
            for (const auto & kv : pass_info.ubos) {
                this->pimpl->parameters.Assign(
                    kv.first, 
                    *(kv.second.get()),
                    kv.second->GetSliceOffset(backbuffer),
                    kv.second->GetSliceSize()
                );
            }
            auto writes_from_layout = tpl->GetReflectedShaderInfo().GenerateDescriptorSetWrite(2, pimpl->parameters);

            std::vector <vk::WriteDescriptorSet> vk_writes {writes_from_layout.buffer.size() + writes_from_layout.image.size()};
            std::vector <std::array<vk::DescriptorBufferInfo, 1>> vk_buffer_writes {writes_from_layout.buffer.size()};

            size_t write_count = 0;
            for (const auto & w : writes_from_layout.buffer) {
                vk_buffer_writes[write_count][0] = vk::DescriptorBufferInfo{
                        std::get<1>(w).buffer,
                        std::get<1>(w).offset,
                        std::get<1>(w).range
                    };
                vk_writes[write_count] = vk::WriteDescriptorSet{
                    pass_info.desc_sets[backbuffer],
                    std::get<0>(w),
                    0,
                    std::get<2>(w),
                    {},
                    vk_buffer_writes[write_count],
                    {}
                };
                write_count ++;
            }

            for (const auto & w : writes_from_layout.image) {
                vk_writes[write_count] = vk::WriteDescriptorSet {
                    pass_info.desc_sets[backbuffer],
                    std::get<0>(w),
                    0,
                    std::get<2>(w),
                    { std::get<1>(w) }
                };
                write_count ++;
            }
            m_system.getDevice().updateDescriptorSets(vk_writes, {});
            pass_info._is_descriptor_dirty[backbuffer] = false;
        }

        // Then do UBO buffer writes
        if (pass_info._is_ubo_dirty[backbuffer]) {
            const auto & splayout = tpl->GetReflectedShaderInfo();

            for (const auto & kv : pass_info.ubos) {
                auto itr = splayout.name_mapping.find(kv.first);
                assert(itr != splayout.name_mapping.end());
                auto pbuf = dynamic_cast<const ShdrRfl::SPInterfaceBuffer *>(itr->second);
                assert(pbuf && pbuf->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer);

                splayout.PlaceBufferVariable(
                    this->pimpl->m_buffer,
                    pbuf,
                    this->pimpl->parameters
                );

                std::memcpy(kv.second->GetSlicePtr(backbuffer), this->pimpl->m_buffer.data(), this->pimpl->m_buffer.size());
            }
            
            pass_info._is_ubo_dirty[backbuffer] = false;
        }
    }

    void MaterialInstance::UpdateGPUInfo(
        const std::string &tag, HomogeneousMesh::MeshVertexType type, uint32_t backbuffer
    ) {
        auto tpl = GetLibrary()->FindMaterialTemplate(tag, type);
        this->UpdateGPUInfo(tpl, backbuffer);
    }

    vk::DescriptorSet MaterialInstance::GetDescriptor(MaterialTemplate *tpl, uint32_t backbuffer) const noexcept {
        auto itr = pimpl->m_pass_infos.find(tpl);
        if (itr == pimpl->m_pass_infos.end())   return nullptr;
        return itr->second.desc_sets[backbuffer];
    }

    vk::DescriptorSet MaterialInstance::GetDescriptor(
        const std::string &tag, HomogeneousMesh::MeshVertexType type, uint32_t backbuffer
    ) const noexcept {
        assert(backbuffer < impl::PassInfo::BACK_BUFFERS);

        auto tpl = GetLibrary()->FindMaterialTemplate(tag, type);
        assert(tpl);
        return this->GetDescriptor(tpl, backbuffer);
    }
    void MaterialInstance::Instantiate(const MaterialAsset &asset) {
        for (const auto & prop : asset.m_properties) {
            auto p = prop.second;
            switch(p.m_type) {
            case MaterialProperty::Type::StorageBuffer:
                break;
            case MaterialProperty::Type::UBO:
                break;
            case MaterialProperty::Type::StorageImage:
                break;
            case MaterialProperty::Type::Texture:
                break;
            default:
                switch(p.m_ubo_type) {
                case MaterialProperty::InBlockVarType::Float:
                    break;
                case MaterialProperty::InBlockVarType::Int:
                    break;
                case MaterialProperty::InBlockVarType::Vec4:
                    break;
                case MaterialProperty::InBlockVarType::Mat4:
                    break;
                default:
                    ;
                }
            }
        }
    }
    std::shared_ptr<MaterialLibrary> MaterialInstance::GetLibrary() const {
        return pimpl->m_library.lock();
    }
} // namespace Engine
