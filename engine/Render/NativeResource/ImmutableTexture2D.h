#ifndef RENDER_NATIVERESOURCE_IMMUTABLETEXTURE2D_INCLUDED
#define RENDER_NATIVERESOURCE_IMMUTABLETEXTURE2D_INCLUDED

#include "ImmutableTexture.h"

namespace Engine
{
    /// @brief An immutable texture of type GL_TEXTURE_2D on GPU.
    class ImmutableTexture2D : public ImmutableTexture {
    public:
        bool Create(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei levels) override;
        bool FullUpload(GLenum format, GLenum type, GLvoid * data) const override;
        void Download(GLvoid * data) const override;
        void Bind() const override;
    };
} // namespace Engine

#endif // RENDER_NATIVERESOURCE_IMMUTABLETEXTURE2D_INCLUDED
