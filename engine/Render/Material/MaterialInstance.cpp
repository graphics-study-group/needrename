#include "MaterialInstance.h"
#include "MaterialTemplateUtils.h"
#include "Render/Memory/ImageInterface.h"
#include "Render/RenderSystem.h"
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
            pass.desc_set = tpl->AllocateDescriptorSet(idx);

            auto ubo_size = tpl->GetMaximalUBOSize(idx);
            if (ubo_size == 0) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Found zero-sized UBO when processing pass %llu of material", ubo_size);
            } else {
                pass.ubo = std::make_unique<Buffer>(system);
                pass.ubo->Create(Buffer::BufferType::Uniform, tpl->GetMaximalUBOSize(idx));
            }

            m_pass_info[idx] = std::move(pass);
        }
    }

    auto MaterialInstance::GetVariables(uint32_t pass_index) const -> const decltype(m_variables.at(0)) &
    {
        return m_variables.at(pass_index);
    }

    void MaterialInstance::WriteTextureUniform(uint32_t pass, uint32_t index, const ImageInterface &texture)
    {
        assert(m_pass_info.contains(pass) && "Cannot find pass.");
        assert(
            m_parent_template.lock()->GetVariable(index, pass).location.set
            && "Cannot find uniform in designated pass."
        );

        m_variables[pass][index] = std::any(std::cref(texture));
        m_pass_info[pass].is_dirty = true;
    }

    void MaterialInstance::WriteUBOUniform(uint32_t pass, uint32_t index, std::any uniform)
    {
        assert(
            MaterialTemplateUtils::REGISTERED_SHADER_UNIFORM_TYPES.find(uniform.type()) 
            != MaterialTemplateUtils::REGISTERED_SHADER_UNIFORM_TYPES.end()
            && "Unsupported uniform type."
        );
        assert(m_pass_info.find(pass) != m_pass_info.end() && "Cannot find pass.");
        assert(
            m_parent_template.lock()->GetVariable(index, pass).location.set
            && "Cannot find uniform in designated pass."
        );

        m_variables[pass][index] = uniform;
        m_pass_info[pass].is_dirty = true;
    }

    void MaterialInstance::WriteDescriptors(uint32_t pass)
    {
        assert(m_pass_info.contains(pass) && "Cannot find pass.");
        auto & pass_info = m_pass_info[pass];

        if (!pass_info.is_dirty)    return;

        auto tpl = m_parent_template.lock();

        // write uniform buffer
        auto & ubo = *(pass_info.ubo.get());
        tpl->PlaceUBOVariables(*this, m_buffer, pass);
        std::memcpy(ubo.Map(), m_buffer.data(), ubo.GetSize());
        ubo.Flush();
        
        // Prepare descriptor writes
        std::vector <vk::WriteDescriptorSet> writes;

        auto image_writes = tpl->GetDescriptorImageInfo(*this, pass);
        writes.reserve(image_writes.size() + 1);
        for (const auto & [binding, image_info] : image_writes) {
            vk::WriteDescriptorSet write {
                    pass_info.desc_set, binding, 0, 1,
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
        pass_info.is_dirty = false;
    }
}
