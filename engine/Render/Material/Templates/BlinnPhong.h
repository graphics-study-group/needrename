#ifndef RENDER_MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED
#define RENDER_MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED

#include "Render/Material/MaterialTemplate.h"
#include "Render/Material/MaterialInstance.h"

namespace Engine {
    class AllocatedImage2DTexture;
    class TransferCommandBuffer;
    namespace Materials {
        /***
         * @brief Built-in asset for classic Blinn-Phong shading.
         * Use it directly with `MaterialTemplate`, or use the helper class `BlinnPhongTemplate`.
         * 
         * Associated with two shaders: `blinn_phong.frag` and `blinn_phong.vert`.
         */
        class BlinnPhongTemplateAsset : public MaterialTemplateAsset {
            std::shared_ptr <ShaderAsset> vs {}, fs {};
            std::shared_ptr <AssetRef> vs_ref {}, fs_ref {};

        public:
            BlinnPhongTemplateAsset();
        };

        class BlinnPhongInstance : public MaterialInstance {
            uint32_t texture_id, specular_id, ambient_id;
        public:
            BlinnPhongInstance(std::weak_ptr <RenderSystem> system, std::shared_ptr<MaterialTemplate> tpl);
            void SetBaseTexture(std::shared_ptr<const AllocatedImage2DTexture> image);
            void SetSpecular(glm::vec4 spec);
            void SetAmbient(glm::vec4 spec);
            void Convert(std::shared_ptr <AssetRef> asset, TransferCommandBuffer & tcb);
        };

        class BlinnPhongTemplate : public MaterialTemplate {
        public:
            BlinnPhongTemplate(std::weak_ptr <RenderSystem> system, std::shared_ptr<AssetRef> asset);
            std::shared_ptr <MaterialInstance> CreateInstance() override;
        };
    }
}

#endif // RENDER_MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED
