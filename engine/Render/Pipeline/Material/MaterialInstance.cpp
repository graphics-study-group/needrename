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
            static constexpr uint32_t BACK_BUFFERS = 2;

            std::unique_ptr<IndexedBuffer> ubo{};

            std::bitset<BACK_BUFFERS> _is_descriptor_set_dirty{};
            std::bitset<BACK_BUFFERS> _is_ubo_dirty{};

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
        auto ubo_size = tpl->GetExpectedUniformBufferSize();
        pass.desc_set = tpl->AllocateDescriptorSet();

        if (ubo_size == 0) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER, "Found zero-sized UBO when processing pass %llu of material", ubo_size
            );
        } else {
            pass.ubo = 
                IndexedBuffer::CreateUnique(
                    system, Buffer::BufferType::Uniform,
                    tpl->GetExpectedUniformBufferSize(),
                    system.GetPhysicalDevice().getProperties().limits.minUniformBufferOffsetAlignment,
                    PassInfo::BACK_BUFFERS,
                    "Indexed UBO for Material"
                );
        }
        pimpl->m_pass_info = std::move(pass);
    }

    MaterialInstance::~MaterialInstance() = default;

    const MaterialTemplate &MaterialInstance::GetTemplate() const {
        return *(pimpl->m_parent_template.lock());
    }

    ShdrRfl::ShaderParameters &MaterialInstance::GetShaderParameters() noexcept {
        return pimpl->m_pass_info.parameters;
    }

    void MaterialInstance::WriteDescriptors() {
        auto &pass_info = pimpl->m_pass_info;
        auto fif = m_system.GetFrameManager().GetTotalFrame() % pass_info.BACK_BUFFERS;
        if (!pass_info._is_descriptor_set_dirty[fif]) return;

        auto tpl = pimpl->m_parent_template.lock();

        // Prepare descriptor writes
        std::vector<vk::WriteDescriptorSet> writes;
        auto writes_from_layout = tpl->GetReflectedShaderInfo().GenerateDescriptorSetWrite(3, this->pimpl->m_pass_info.parameters);

        writes.reserve(writes_from_layout.buffer.size() + writes_from_layout.image.size());
        for (const auto &[binding, image_info] : writes_from_layout.image) {
            vk::WriteDescriptorSet write{
                pass_info.desc_set,
                binding,
                0,
                1,
                // TODO: We need a better way to check if its storage image or texture image.
                image_info.imageLayout == vk::ImageLayout::eGeneral ? vk::DescriptorType::eStorageImage
                                                                    : vk::DescriptorType::eCombinedImageSampler,
                &image_info,
                nullptr,
                nullptr
            };
            writes.push_back(write);
        }

        // write ubo descriptors
        if (pass_info.ubo) {
            auto &ubo = *(pass_info.ubo.get());
            std::array<vk::DescriptorBufferInfo, 1> ubo_buffer_info = {
                vk::DescriptorBufferInfo{ubo.GetBuffer(), ubo.GetSliceOffset(fif), ubo.GetSliceSize()}
            };
            vk::WriteDescriptorSet ubo_write{
                pass_info.desc_set, 0, 0, vk::DescriptorType::eUniformBuffer, {}, ubo_buffer_info, {}
            };
            writes.push_back(ubo_write);
        }

        m_system.getDevice().updateDescriptorSets(writes, {});
        pass_info._is_descriptor_set_dirty.reset(fif);
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
