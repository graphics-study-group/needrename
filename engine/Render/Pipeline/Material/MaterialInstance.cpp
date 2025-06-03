#include "MaterialInstance.h"
#include "Render/Pipeline/PipelineUtils.h"
#include "Render/Memory/ImageInterface.h"
#include <Render/Memory/Image2DTexture.h>
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Texture/Image2DTextureAsset.h>
#include <SDL3/SDL.h>

namespace Engine {
    MaterialInstance::MaterialInstance(
        std::weak_ptr <RenderSystem> system, 
        std::shared_ptr<MaterialTemplate> tpl
        ) : m_system(system), m_parent_template(tpl)
    {
        // Allocate uniform buffers and per-material descriptor sets
        for (const auto & [idx, info] : tpl->GetAllPassInfo()) {
            PassInfo pass{};
            auto ubo_size = tpl->GetMaximalUBOSize(idx);
            if (ubo_size == 0) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Found zero-sized UBO when processing pass %llu of material", ubo_size);
            } else {
                pass.desc_set = tpl->AllocateDescriptorSet(idx);
                pass.ubo = std::make_unique<Buffer>(system);
                pass.ubo->Create(Buffer::BufferType::Uniform, tpl->GetMaximalUBOSize(idx));
            }
            m_pass_info[idx] = std::move(pass);
        }
    }

    const MaterialTemplate &MaterialInstance::GetTemplate() const
    {
        return *m_parent_template.lock();
    }

    auto MaterialInstance::GetInBlockVariables(uint32_t pass_index) const -> const decltype(m_inblock_variables.at(0)) &
    {
        return m_inblock_variables.at(pass_index);
    }

    auto MaterialInstance::GetDescVariables(uint32_t pass_index) const -> const decltype(m_desc_variables.at(0)) &
    {
        return m_desc_variables.at(pass_index);
    }

    void MaterialInstance::WriteTextureUniform(uint32_t pass, uint32_t index, std::shared_ptr <const ImageInterface> texture)
    {
        assert(m_pass_info.contains(pass) && "Cannot find pass.");
        assert(
            m_parent_template.lock()->GetDescVariable(index, pass).set
            && "Cannot find uniform in designated pass."
        );

        m_desc_variables[pass][index] = std::any(texture);
        m_pass_info[pass].is_descriptor_set_dirty = true;
    }

    void MaterialInstance::WriteStorageBufferUniform(uint32_t pass, uint32_t index, std::shared_ptr <const Buffer> buffer)
    {
        assert(false && "Unimplemented");
    }

    void MaterialInstance::WriteUBOUniform(uint32_t pass, uint32_t index, std::any uniform)
    {
        assert(
            PipelineUtils::REGISTERED_SHADER_UNIFORM_TYPES.find(uniform.type()) 
            != PipelineUtils::REGISTERED_SHADER_UNIFORM_TYPES.end()
            && "Unsupported uniform type."
        );
        assert(m_pass_info.find(pass) != m_pass_info.end() && "Cannot find pass.");
        assert(
            m_parent_template.lock()->GetInBlockVariable(index, pass).block_location.set
            && "Cannot find uniform in designated pass."
        );

        m_inblock_variables[pass][index] = uniform;
        m_pass_info[pass].is_ubo_dirty = true;
    }

    void MaterialInstance::WriteUBO(uint32_t pass)
    {
        assert(m_pass_info.contains(pass) && "Cannot find pass.");
        auto & pass_info = m_pass_info[pass];
        if (!pass_info.is_ubo_dirty)    return;

        auto tpl = m_parent_template.lock();

        // write uniform buffer
        auto & ubo = *(pass_info.ubo.get());
        tpl->PlaceUBOVariables(*this, m_buffer, pass);
        std::memcpy(ubo.Map(), m_buffer.data(), ubo.GetSize());
        ubo.Flush();

        pass_info.is_ubo_dirty = false;
    }

    void MaterialInstance::WriteDescriptors(uint32_t pass)
    {
        assert(m_pass_info.contains(pass) && "Cannot find pass.");
        auto & pass_info = m_pass_info[pass];

        if (!pass_info.desc_set)    return;
        if (!pass_info.is_descriptor_set_dirty)    return;

        auto tpl = m_parent_template.lock();
        auto & ubo = *(pass_info.ubo.get());
        
        // Prepare descriptor writes
        std::vector <vk::WriteDescriptorSet> writes;

        auto image_writes = tpl->GetDescriptorImageInfo(*this, pass);
        writes.reserve(image_writes.size() + 1);
        for (const auto & [binding, image_info] : image_writes) {
            vk::WriteDescriptorSet write {
                    pass_info.desc_set, binding, 0, 1,
                    // TODO: We need a better way to check if its storage image or texture image.
                    image_info.imageLayout == vk::ImageLayout::eGeneral ? 
                        vk::DescriptorType::eStorageImage : 
                        vk::DescriptorType::eCombinedImageSampler,
                    &image_info, nullptr, nullptr
            };
            writes.push_back(write);
        }

        // write ubo descriptors
        std::array <vk::DescriptorBufferInfo, 1> ubo_buffer_info = {
            vk::DescriptorBufferInfo{ubo.GetBuffer(), 0, vk::WholeSize}
        };
        vk::WriteDescriptorSet ubo_write {
            pass_info.desc_set, 0, 0,
            vk::DescriptorType::eUniformBuffer,
            {},
            ubo_buffer_info,
            {}
        };
        writes.push_back(ubo_write);

        m_system.lock()->getDevice().updateDescriptorSets(writes, {});
        pass_info.is_descriptor_set_dirty = false;
    }
    vk::DescriptorSet MaterialInstance::GetDescriptor(uint32_t pass) const
    {
        return m_pass_info.at(pass).desc_set;
    }
    void MaterialInstance::Convert(std::shared_ptr <AssetRef> asset)
    {
        assert(false && "Unimplemented");
    }
}
