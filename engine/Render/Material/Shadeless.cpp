#include "Shadeless.h"
#include "Render/NativeResource/ShaderPass.h"
#include "Render/NativeResource/ImmutableTexture2D.h"

#include <cassert>

const char vert [] = R"(
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 uv;

out vec3 clip_space_coordinate;
out vec2 vert_uv;

void main() {
    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
    clip_space_coordinate = aPos;
    vert_uv = uv;
}
)";

const char frag [] = R"(
#version 330 core

in vec3 clip_space_coordinate;
in vec2 vert_uv;
uniform sampler2D albedo;

out vec4 color;
void main() {
    color = texture(albedo, vert_uv);
    // color = vec4(vert_uv.x / 2.0, vert_uv.y / 2.0, 0.0, 1.0);
}
)";

namespace Engine
{
    std::unique_ptr <ShaderPass> ShadelessMaterial::pass;
    GLint ShadelessMaterial::location_albedo;

    ShadelessMaterial::ShadelessMaterial(std::shared_ptr<RenderSystem> system) : Material(system)
    {
        if (!pass) {
            pass = std::make_unique <ShaderPass> ();
            auto result = pass->Compile(vert, frag);
            assert(result);

            location_albedo = pass->GetUniform("albedo");
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

    void ShadelessMaterial::PrepareDraw()
    {
        pass->Use();
        m_albedo->BindToLocation(0);
        ShaderPass::SetUniformInteger(location_albedo, 0);
    }

} // namespace Engine

