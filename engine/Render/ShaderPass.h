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

        /// @brief Get the unique identifier of the program.
        /// @return identifier of the program
        GLuint GetProgram() const noexcept;

        /// @brief Setup OpenGL pipeline to use the program
        void Use() const noexcept;

        /// @brief Check whether the pass is already compiled and linked.
        /// @return true if already linked, false otherwise.
        bool IsLinked() const noexcept;

        /// @brief Compile and link the shader pass into a program usable by pipeline.
        /// @param vertex Vertex shader source code
        /// @param fragment Fragment shader source code
        /// @return true if compilation is successful, false otherwise.
        bool Compile(const char * vertex, const char * fragment);

        /// @brief Get layout location of a given uniform variable
        /// @param name Name of the uniform
        /// @return layout location of the uniform, -1 if not available.
        /// @note To modify its uniforms, the program must be set active by glUseProgram.
        GLint GetUniform(const char * name) const noexcept;
    protected:
        GLuint m_shaderProgram;
    };
};

#endif // RENDER_SHADERPASS_INCLUDED
