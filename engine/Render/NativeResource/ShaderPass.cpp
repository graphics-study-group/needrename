#include "ShaderPass.h"
#include <format>
#include <cassert>
#include <stdexcept>
#include <SDL3/SDL.h>

namespace Engine
{
    void ShaderPass::Use() const noexcept
    {
        assert(this->IsValid() && "Trying to use unlinked program");
        glUseProgram(m_handle);
    }

    ShaderPass::~ShaderPass()
    {
        this->Release();
    }

    bool ShaderPass::IsValid() const noexcept
    {
        return glIsProgram(m_handle);
    }

    void ShaderPass::Release() noexcept
    {
        if (this->IsValid()) {
            glDeleteProgram(m_handle);
        }
        this->m_handle = 0;
    }

    bool ShaderPass::Compile(const char *vertex, const char *fragment)
    {
        assert((!this->IsValid()) && "Re-compiling a shader pass program.");
        assert(!glGetError());

        GLenum glError = GL_NO_ERROR;
        GLint success = GL_TRUE;

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            SDL_LogError(0, "Failed to create vertex shader, OpenGL error %d", glError);
            return false;
        }

        glShaderSource(vertexShader, 1, &vertex, nullptr);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            SDL_LogError(0, "Failed to set vertex shader source, OpenGL error %d", glError);
            glDeleteShader(vertexShader);
            return false;
        }

        glCompileShader(vertexShader);
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (success != GL_TRUE) {
            GLchar log[128];
            glGetShaderInfoLog(vertexShader, sizeof log, nullptr, log);
            SDL_LogError(0, "Failed to compile vertex shader, log: %s", log);
            glDeleteShader(vertexShader);
            return false;
        }

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            SDL_LogError(0, "Failed to create fragment shader, OpenGL error %d", glError);
            glDeleteShader(vertexShader);
            return false;
        }

        glShaderSource(fragmentShader, 1, &fragment, nullptr);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            SDL_LogError(0, "Failed to set fragment shader source, OpenGL error %d", glError);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            return false;
        }

        glCompileShader(fragmentShader);
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (success != GL_TRUE) {
            GLchar log[128];
            glGetShaderInfoLog(fragmentShader, sizeof log, nullptr, log);
            SDL_LogError(0, "Failed to compile fragment shader, log: %s", log);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            return false;
        }

        m_handle = glCreateProgram();
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            SDL_LogError(0, "Failed to create program, OpenGL error %d", glError);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            this->Release();
            return false;
        }

        glAttachShader(m_handle, vertexShader);
        glAttachShader(m_handle, fragmentShader);
        glLinkProgram(m_handle);
        glDetachShader(m_handle, vertexShader);
        glDetachShader(m_handle, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        glGetProgramiv(m_handle, GL_LINK_STATUS, &success);
        if (success != GL_TRUE) {
            GLchar log[128];
            glGetProgramInfoLog(m_handle, sizeof log, nullptr, log);
            SDL_LogError(0, "Failed to link shader program, log: %s", log);
            this->Release();
            return false;
        }
        return true;
    }

    GLint ShaderPass::GetUniform(const char * name) const noexcept
    {
        static_assert(std::is_same_v<GLchar, char> == true, "GLchar and char should be the same type.");
        assert(this->IsValid() && "Trying to get uniform location from uncompiled program.");

        return glGetUniformLocation(m_handle, name);
    }

    GLint ShaderPass::GetAttribute(const char *name) const noexcept
    {
        assert(this->IsValid() && "Trying to get attribute location from uncompiled program.");
        return glGetAttribLocation(m_handle, name);
    }

} // namespace Engine

