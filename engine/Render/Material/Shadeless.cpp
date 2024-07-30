#include "Shadeless.h"
#include "Render/NativeResource/ShaderPass.h"
#include "Render/NativeResource/ImmutableTexture2D.h"
#include "Framework/component/RenderComponent/RendererComponent.h"

#include <cassert>

constexpr std::string_view vert = R"(
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 uv;

uniform mat4 model_matrix;

out vec3 clip_space_coordinate;
out vec2 vert_uv;

void main() {
    gl_Position = model_matrix * vec4(aPos.x, aPos.y, aPos.z, 1.0);
    clip_space_coordinate = aPos;
    vert_uv = uv;
}
)";

constexpr std::string_view frag = R"(
#version 330 core

in vec3 clip_space_coordinate;
in vec2 vert_uv;

uniform sampler2D albedo;
uniform sampler2D normal;

out vec4 color;
void main() {
    color = texture(albedo, vert_uv);
    // color = vec4(vert_uv.x / 2.0, vert_uv.y / 2.0, 0.0, 1.0);
}
)";

namespace Engine
{
    std::unique_ptr <ShaderPass> ShadelessMaterial::pass;
    GLint ShadelessMaterial::location_model_matrix;
    GLint ShadelessMaterial::location_albedo;
    GLint ShadelessMaterial::location_normal;

    ShadelessMaterial::ShadelessMaterial(std::shared_ptr<RenderSystem> system) : Material(system)
    {
        if (!pass) {
            pass = std::make_unique <ShaderPass> ();

            // Note that std::string_view::data() do not guaruantee a null-terminated string.
            // However in this case string-literals are indeed null-terminated.
            auto result = pass->Compile(vert.data(), frag.data());
            assert(result);

            location_model_matrix = pass->GetUniform("model_matrix");
            location_albedo = pass->GetUniform("albedo");
            // location_normal = pass->GetUniform("normal");
            assert(location_albedo >= 0);
        }
    }

    ShadelessMaterial::~ShadelessMaterial()
    {
    }

    void ShadelessMaterial::SetAlbedo(std::shared_ptr<ImmutableTexture2D> texture) noexcept
    {
        assert(texture);
        this->m_albedo = texture;
    }

    void ShadelessMaterial::PrepareDraw(const CameraContext & cameraContext, const RendererContext & rendererContext)
    {
        assert(m_albedo && "Albedo texture is missing.");

        pass->Use();
        glUniformMatrix4fv(location_model_matrix, 1, GL_FALSE, &rendererContext.model_matrix[0][0]);
        m_albedo->BindToLocation(0);
        ShaderPass::SetUniformInteger(location_albedo, 0);

        if (m_normal) {
            m_normal->BindToLocation(1);
            ShaderPass::SetUniformInteger(location_normal, 1);
        }
    }

} // namespace Engine

