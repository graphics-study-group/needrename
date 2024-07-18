#ifndef RENDER_SHADER_INCLUDED
#define RENDER_SHADER_INCLUDED

#include "NativeResource.h"

namespace Engine
{
    class Shader : public NativeResource {
    public:
        Shader() = default;
        virtual ~Shader();

        /// @note To fully delete a shader, the program attached to must be unlinked firstly. 
        virtual void Release() noexcept override;

        virtual bool IsValid() const noexcept override;

        bool Compile(GLenum type, const char * source);
    };
} // namespace Engine


#endif // RENDER_SHADER_INCLUDED
