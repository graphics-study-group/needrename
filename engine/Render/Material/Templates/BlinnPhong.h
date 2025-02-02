#ifndef RENDER_MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED
#define RENDER_MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED

#include "Render/Material/MaterialTemplate.h"
#include "Render/Material/MaterialInstance.h"

namespace Engine {
    namespace Materials {
        /***
         * @brief Built-in asset for classic Blinn-Phong shading.
         * Use it directly with `MaterialTemplate`, or use the helper class `BlinnPhongTemplate`.
         * 
         * Associated with two shaders: `blinn_phong.frag` and `blinn_phong.vert`.
         */
        class BlinnPhongAsset : public MaterialTemplateAsset {
            std::shared_ptr <ShaderAsset> vs, fs;
            std::shared_ptr <AssetRef> vs_ref, fs_ref;

        public:
            BlinnPhongAsset();
        };

        class BlinnPhongInstance : public MaterialInstance {

        };

        class BlinnPhongTemplate : public MaterialTemplate {

        };
    }
}

#endif // RENDER_MATERIAL_TEMPLATES_BLINNPHONG_INCLUDED
