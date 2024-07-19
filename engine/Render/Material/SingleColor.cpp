#include "SingleColor.h"

#include "Render/NativeResource/ShaderPass.h"

#include <string_view>
#include <cassert>

constexpr std::string_view vert = R"(
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 uv;

void main() {
    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
}
)";

constexpr std::string_view frag = R"(
#version 330 core
uniform vec4 in_color;

out vec4 color;
void main() {
    color = in_color;
}
)";

namespace Engine {
    std::unique_ptr <ShaderPass> SingleColor::pass;
    GLint SingleColor::location_color;

    SingleColor::SingleColor(std::shared_ptr<RenderSystem> system, GLfloat _r, GLfloat _g, GLfloat _b, GLfloat _a)
        : Material(system), r(_r), g(_g), b(_b), a(_a)
    {
        if (!pass) {
            pass = std::make_unique <ShaderPass> ();

            // Note that std::string_view::data() do not guaruantee a null-terminated string.
            // However in this case string-literals are indeed null-terminated.
            auto result = pass->Compile(vert.data(), frag.data());
            assert(result);

            location_color = pass->GetUniform("in_color");
            // location_normal = pass->GetUniform("normal");
            assert(location_color >= 0);
        }
    }

    SingleColor::~SingleColor()
    {
    }

    void SingleColor::PrepareDraw()
    {
        pass->Use();
        glUniform4f(location_color, r, g, b, a);
    }

}
