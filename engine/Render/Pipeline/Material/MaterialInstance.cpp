#include "MaterialInstance.h"

#include "Asset/Material/MaterialAsset.h"
#include "Render/Memory/ShaderParameters/ShaderParameter.h"
#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"
#include "Render/Pipeline/PipelineInfo.h"
#include "Render/Pipeline/PipelineUtils.h"
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
            static constexpr uint32_t BACK_BUFFERS = 3;

            std::unordered_map <std::string, std::unique_ptr<IndexedBuffer>> ubos{};

            std::bitset<8> _is_ubo_dirty{};
            std::bitset<8> _is_descriptor_dirty{};

            vk::DescriptorSet desc_set{};
            ShdrRfl::ShaderParameters parameters{};
        };

        std::weak_ptr<MaterialTemplate> m_parent_template;

        PassInfo m_pass_info{};

        // A small buffer for uniform buffer staging to avoid random write to UBO.
        std::vector<std::byte> m_buffer{};
    };

    MaterialInstance::MaterialInstance(RenderSystem &system, std::shared_ptr<MaterialTemplate> tpl) :
        m_system(system), pimpl(std::make_unique<impl>(tpl)) {
        using PassInfo = impl::PassInfo;
        // Allocate uniform buffers and per-material descriptor sets
        const auto & info = tpl->GetPassInfo();
        PassInfo pass{};
        const auto & splayout = tpl->GetReflectedShaderInfo();
        pass.desc_set = tpl->AllocateDescriptorSet();

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

        pimpl->m_pass_info = std::move(pass);
    }

    MaterialInstance::~MaterialInstance() = default;

    const MaterialTemplate &MaterialInstance::GetTemplate() const {
        return *(pimpl->m_parent_template.lock());
    }

    void MaterialInstance::AssignSimpleVariables(
        const std::string &name,
        std::variant<uint32_t, int32_t, float> value
    ) {
        this->pimpl->m_pass_info._is_ubo_dirty.set();
        this->pimpl->m_pass_info.parameters.Assign(name, value);
    }

    void MaterialInstance::AssignSimpleVariables(const std::string &name, std::variant<glm::vec4, glm::mat4> value) {
        this->pimpl->m_pass_info._is_ubo_dirty.set();
        this->pimpl->m_pass_info.parameters.Assign(name, value);
    }

    void MaterialInstance::AssignTexture(const std::string &name, const Texture &texture) {
        this->pimpl->m_pass_info._is_descriptor_dirty.set();
        this->pimpl->m_pass_info.parameters.Assign(name, texture);
    }

    void MaterialInstance::AssignBuffer(const std::string &name, const Buffer &buffer) {
        this->pimpl->m_pass_info._is_descriptor_dirty.set();
        this->pimpl->m_pass_info.parameters.Assign(name, buffer);
    }

    const ShdrRfl::ShaderParameters &MaterialInstance::GetShaderParameters() const noexcept {
        return pimpl->m_pass_info.parameters;
    }

    void MaterialInstance::UpdateGPUInfo(uint32_t backbuffer) {
            if (backbuffer > pimpl->m_pass_info.BACK_BUFFERS) {
                backbuffer = m_system.GetFrameManager().GetFrameInFlight();
            }
            
            auto tpl = pimpl->m_parent_template.lock();

            // First prepare descriptor writes
            if (pimpl->m_pass_info._is_descriptor_dirty[backbuffer]) {
                // Point UBOs to internal buffers
                for (const auto & kv : pimpl->m_pass_info.ubos) {
                    this->pimpl->m_pass_info.parameters.Assign(kv.first, *(kv.second.get()));
                }
                auto writes_from_layout = tpl->GetReflectedShaderInfo().GenerateDescriptorSetWrite(3, this->pimpl->m_pass_info.parameters);

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
                        pimpl->m_pass_info.desc_set,
                        std::get<0>(w),
                        0,
                        std::get<2>(w) == vk::DescriptorType::eUniformBuffer ? 
                            vk::DescriptorType::eUniformBufferDynamic : std::get<2>(w),
                        {},
                        vk_buffer_writes[write_count],
                        {}
                    };
                    write_count ++;
                }

                for (const auto & w : writes_from_layout.image) {
                    vk_writes[write_count] = vk::WriteDescriptorSet {
                        pimpl->m_pass_info.desc_set,
                        std::get<0>(w),
                        0,
                        std::get<2>(w),
                        { std::get<1>(w) }
                    };
                    write_count ++;
                }
                m_system.getDevice().updateDescriptorSets(vk_writes, {});
                pimpl->m_pass_info._is_descriptor_dirty[backbuffer] = false;
            }

            // Then do UBO buffer writes
            if (pimpl->m_pass_info._is_ubo_dirty[backbuffer]) {
                const auto & splayout = tpl->GetReflectedShaderInfo();

                for (const auto & kv : this->pimpl->m_pass_info.ubos) {
                    auto itr = splayout.name_mapping.find(kv.first);
                    assert(itr != splayout.name_mapping.end());
                    auto pbuf = dynamic_cast<const ShdrRfl::SPInterfaceBuffer *>(itr->second);
                    assert(pbuf && pbuf->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer);

                    splayout.PlaceBufferVariable(
                        this->pimpl->m_buffer,
                        pbuf,
                        this->pimpl->m_pass_info.parameters
                    );

                    std::memcpy(kv.second->GetSlicePtr(backbuffer), this->pimpl->m_buffer.data(), this->pimpl->m_buffer.size());
                }
                
                pimpl->m_pass_info._is_ubo_dirty[backbuffer] = false;
            }
    }

    std::vector<uint32_t> MaterialInstance::GetDynamicUBOOffset(uint32_t backbuffer) {
        if (backbuffer > pimpl->m_pass_info.BACK_BUFFERS) {
            backbuffer = m_system.GetFrameManager().GetFrameInFlight();
        }
        auto tpl = pimpl->m_parent_template.lock();
        const auto & splayout = tpl->GetReflectedShaderInfo();

        std::vector <uint32_t> ret;
        ret.reserve(pimpl->m_pass_info.ubos.size());

        // This vector is already sorted by bindings, so we don't need to sort again.
        for (const auto & pint : splayout.interfaces) {
            if (auto pbuf = dynamic_cast<const ShdrRfl::SPInterfaceBuffer *>(pint)) {
                if (pbuf->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer) {
                    assert(pbuf->layout_set == 3);
                    auto itr = pimpl->m_pass_info.ubos.find(pbuf->name);
                    assert(itr != pimpl->m_pass_info.ubos.end());
                    ret.push_back(itr->second->GetSliceOffset(backbuffer));
                }
            }
        }
        return ret;
    }

    vk::DescriptorSet MaterialInstance::GetDescriptor() const {
        return pimpl->m_pass_info.desc_set;
    }
    void MaterialInstance::Instantiate(const MaterialAsset &asset) {
        const auto &pass = pimpl->m_parent_template.lock()->GetPassInfo();

        for (const auto prop : asset.m_properties) {
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
                }
            }
        }
    }
} // namespace Engine
