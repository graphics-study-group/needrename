#include "ImmutableTexture2D.h"

#include <cassert>
#include <iostream>
#include <SDL3/SDL.h>
#include <stb_image.h>

namespace Engine
{
    bool ImmutableTexture2D::Create(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei levels)
    {
        assert(depth == 0);
        assert((!this->IsValid()) && "Re-creating an immutable texture.");
        assert(!glGetError());

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
            this->Release();
            return false;
        }

        this->dimensions = std::make_tuple(width, height, 0);
        return true;
    }

    bool ImmutableTexture2D::FullUpload(GLenum format, GLenum type, GLvoid *data) const
    {
        assert(this->IsValid());
        assert(!glGetError());

        this->Bind();

        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, std::get<0>(dimensions), std::get<1>(dimensions), format, type, data);
        GLenum glError = glGetError();
        if (glError) {
            SDL_LogError(0, "Failed to upload immutable texture, OpenGL error %d.", glError);
            return false;
        }
        glGenerateMipmap(GL_TEXTURE_2D);
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

    bool ImmutableTexture2D::LoadFromFile(const char * filename, GLenum textureFormat, GLuint levels)
    {
        int width, height, channels;
        int ok = stbi_info(filename, &width, &height, &channels);
        if (!ok) {
            SDL_LogError(0, "Cannot load image file %s information, format unsupported.", filename);
            return false;
        }
        if (channels != 3 && channels != 4) {
            SDL_LogError(0, "Image file %s contains unexpected color format.", filename);
            return false;
        }

        if (!this->Create(width, height, 0, textureFormat, levels)) {
            return false;
        }

        GLint alignment;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        if (alignment > 0) {
            SDL_LogWarn(0, "Pixel alignment is not set to 1, setting it to 1.");
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        }
        stbi_set_flip_vertically_on_load(true);
        unsigned char * data = stbi_load(filename, &width, &height, &channels, 0);
        if (data == nullptr) {
            SDL_LogError(0, "Cannot load image file %s, STB reports %s.", filename, stbi_failure_reason());
            return false;
        }
        bool result;
        if (channels == 4)
            result = this->FullUpload(GL_RGBA, GL_UNSIGNED_BYTE, (void *) data);
        else
            result = this->FullUpload(GL_RGB, GL_UNSIGNED_BYTE, (void *) data);
        stbi_image_free(data);

        return result;
    }

} // namespace Engine
