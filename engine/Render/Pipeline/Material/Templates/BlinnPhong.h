#ifndef MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED
#define MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED

#include "Render/Pipeline/Material/MaterialTemplate.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include <glm.hpp>

namespace Engine {
    class SampledTexture;
    namespace Materials {
        class BlinnPhongInstance : public MaterialInstance {
            uint32_t texture_id{}, specular_id{}, ambient_id{};
        public:
            BlinnPhongInstance(std::weak_ptr <RenderSystem> system, std::shared_ptr<MaterialTemplate> tpl);
            void SetBaseTexture(std::shared_ptr<const SampledTexture> image);
            void SetSpecular(glm::vec4 spec);
            void SetAmbient(glm::vec4 spec);
            void Instantiate(const MaterialAsset & asset) override;
        };

        class BlinnPhongTemplate : public MaterialTemplate {
        public:
            BlinnPhongTemplate(std::weak_ptr <RenderSystem> system, std::shared_ptr<AssetRef> asset);
            std::shared_ptr <MaterialInstance> CreateInstance() override;
        };
    }
}

#endif // MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED
