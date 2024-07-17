#ifndef RENDER_SHADERPASS_INCLUDED
#define RENDER_SHADERPASS_INCLUDED

#include "NativeResource.h"

namespace Engine {
    /// A single pass consists of vertex and fragment shaders.
    /// Represents an OpenGL program.
    class ShaderPass : public NativeResource {
    public:
        ShaderPass() = default;
        ~ShaderPass();

        bool IsValid() const noexcept override;
        void Release() noexcept override;

        /// @brief Setup OpenGL pipeline to use the program
        void Use() const noexcept;

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
    };
};

#endif // RENDER_SHADERPASS_INCLUDED
