#ifndef RENDER_MATERIAL_SHADELESS_INCLUDED
#define RENDER_MATERIAL_SHADELESS_INCLUDED

#include "Material.h"
#include <glad/glad.h>

namespace Engine {
    class ShaderPass;
    class ImmutableTexture2D;

    class ShadelessMaterial : public Material {
    public:
        ShadelessMaterial();
        ~ShadelessMaterial();

        void SetAlbedo(std::shared_ptr <ImmutableTexture2D> texture) noexcept;
        virtual void Load() override;
        virtual void Unload() override;
        virtual void PrepareDraw(const CameraContext & CameraContext, const RendererContext & RendererContext) override;

    protected:
        static std::unique_ptr <ShaderPass> pass;

        static GLint location_model_matrix;
        static GLint location_projection_matrix;
        static GLint location_view_matrix;
        static GLint location_albedo;
        static GLint location_normal;

        std::shared_ptr <ImmutableTexture2D> m_albedo;
        std::shared_ptr <ImmutableTexture2D> m_normal;
    };
}

#endif // RENDER_MATERIAL_SHADELESS_INCLUDED
