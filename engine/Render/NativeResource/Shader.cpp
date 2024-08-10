#include "Shader.h"

#include <cassert>
#include <SDL3/SDL.h>

namespace Engine
{
    Shader::~Shader()
    {
        this->Release();
    }

    void Shader::Release() noexcept
    {
        if (this->IsNativeValid()) {
            glDeleteShader(m_handle);
        }
        m_handle = 0;
    }

    bool Shader::IsNativeValid() const noexcept
    {
        return glIsShader(m_handle);
    }

    bool Shader::Compile(GLenum type, const char *source)
    {
        assert((!this->IsNativeValid()) && "Re-compiling existing shader.");
        assert(!glGetError());

        GLenum glError;
        m_handle = glCreateShader(type);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            SDL_LogError(0, "Failed to create shader, OpenGL error %d", glError);
            return false;
        }

        glShaderSource(m_handle, 1, &source, nullptr);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            SDL_LogError(0, "Failed to set shader source, OpenGL error %d", glError);
            this->Release();
            return false;
        }

        int success;
        glCompileShader(m_handle);
        glGetShaderiv(m_handle, GL_COMPILE_STATUS, &success);
        if (success != GL_TRUE) {
            GLchar log[128];
            glGetShaderInfoLog(m_handle, sizeof log, nullptr, log);
            SDL_LogError(0, "Failed to compile shader, log: %s", log);
            this->Release();
            return false;
        }
        return true;
    }

} // namespace Engine
