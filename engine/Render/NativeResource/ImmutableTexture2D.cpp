#include "ImmutableTexture2D.h"
#include <cassert>
#include <iostream>
#include <SDL3/SDL.h>

namespace Engine
{
    bool ImmutableTexture2D::Create(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei levels)
    {
        assert(depth == 0);
        assert((!this->IsValid()) && "Re-creating an immutable texture.");
        GLenum glError;

        glGenTextures(1, &m_handle);
        glBindTexture(GL_TEXTURE_2D, m_handle);
        glError = glGetError();
        if (glError) {
            SDL_LogError(0, "Failed to generate texture, OpenGL error %d.", glError);
            return false;
        }

        glTexStorage2D(GL_TEXTURE_2D, levels, format, width, height);
        glError = glGetError();
        if (glError) {
            SDL_LogError(0, "Failed to allocate immutable texture, OpenGL error %d.", glError);
            return false;
        }

        this->dimensions = std::make_tuple(width, height, 0);
        return true;
    }

    bool ImmutableTexture2D::FullUpload(GLenum format, GLenum type, GLvoid *data) const
    {
        assert(this->IsValid());
        this->Bind();

        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, std::get<0>(dimensions), std::get<1>(dimensions), format, type, data);
        GLenum glError = glGetError();
        if (glError) {
            SDL_LogError(0, "Failed to upload immutable texture, OpenGL error %d.", glError);
            return false;
        }
        return true;
    }

    void ImmutableTexture2D::Download(GLvoid *data) const
    {
        SDL_LogCritical(SDL_LogCategory::SDL_LOG_CATEGORY_APPLICATION, "Unimplemented.");
    }

    void ImmutableTexture2D::Bind() const
    {
        assert(this->IsValid());
        glBindTexture(GL_TEXTURE_2D, m_handle);
    }

} // namespace Engine
