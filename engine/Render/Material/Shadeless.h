#ifndef RENDER_MATERIAL_SHADELESS_INCLUDED
#define RENDER_MATERIAL_SHADELESS_INCLUDED

#include "Material.h"
#include <glad/glad.h>

namespace Engine {
    class ShaderPass;
    class ImmutableTexture2D;

    class ShadelessMaterial : public Material {
    public:
        ShadelessMaterial(std::shared_ptr <RenderSystem> system);
        ~ShadelessMaterial();

        void SetAlbedo(std::shared_ptr <ImmutableTexture2D> texture) noexcept;
        void virtual PrepareDraw(const MaterialDrawContext* context) override;

    protected:
        static std::unique_ptr <ShaderPass> pass;

        static GLint location_model_matrix;
        static GLint location_albedo;
        static GLint location_normal;

        std::shared_ptr <ImmutableTexture2D> m_albedo;
        std::shared_ptr <ImmutableTexture2D> m_normal;
    };
}

#endif // RENDER_MATERIAL_SHADELESS_INCLUDED
