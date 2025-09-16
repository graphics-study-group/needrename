#ifndef MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED
#define MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED

#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialTemplate.h"
#include <glm.hpp>

namespace Engine {
    class RenderSystem;
    class Texture;

    namespace Materials {
        class BlinnPhongInstance : public MaterialInstance {
            std::shared_ptr <Texture> base_texture{};

        public:
            BlinnPhongInstance(RenderSystem &system, std::shared_ptr<MaterialTemplate> tpl);
            void SetBaseTexture(std::shared_ptr<const Texture> image);
            void SetSpecular(glm::vec4 spec);
            void SetAmbient(glm::vec4 spec);
            void Instantiate(const MaterialAsset &asset) override;
        };

        class BlinnPhongTemplate : public MaterialTemplate {
        public:
            BlinnPhongTemplate(RenderSystem &system);
            std::shared_ptr<MaterialInstance> CreateInstance() override;
        };
    } // namespace Materials
} // namespace Engine

#endif // MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED
