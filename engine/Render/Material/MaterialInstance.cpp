#include "MaterialInstance.h"
#include "MaterialTemplateUtils.h"
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
    }
}
