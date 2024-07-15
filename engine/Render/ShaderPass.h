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

        /// @brief Get layout location of a given uniform variable. 
        /// To modify its uniforms with glUniform, the program must be set active by glUseProgram.
        /// @param name Name of the uniform
        /// @return layout location of the uniform, -1 if not available.
        GLint GetUniform(const char * name) const noexcept;

        /// @brief Get layout location of a give atttribute name
        /// @param name Name of the attribute
        /// @return layout location of the attribute, -1 if not available.
        /// @note Consider using layout directive with index instead of this method.
        GLint GetAttribute(const char * name) const noexcept;
    protected:
        GLuint m_shaderProgram;
    };
};

#endif // RENDER_SHADERPASS_INCLUDED
