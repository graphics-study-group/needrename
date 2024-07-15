#ifndef RENDER_SHADERPASS_INCLUDED
#define RENDER_SHADERPASS_INCLUDED

#include <GLAD/glad.h>

namespace Engine {
    /// A single pass consists of vertex and fragment shaders.
    /// Represents an OpenGL program.
    class ShaderPass {
    public:
        ShaderPass();
        ~ShaderPass();
        
        // Do not allow copy, since destruction of copied program causes unwanted release of GPU resource
        ShaderPass (const ShaderPass &) = delete;
        void operator = (const ShaderPass &) = delete;

        GLuint GetProgram() const noexcept;
        void Use() const;

        bool IsLinked() const noexcept;
        bool Compile(const char * vertex, const char * fragment);

        GLuint GetUniform(const char * name) const noexcept;
    protected:
        GLuint m_shaderProgram;
    };
};

#endif // RENDER_SHADERPASS_INCLUDED
