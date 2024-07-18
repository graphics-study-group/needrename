#include "ShaderPass.h"
#include "Shader.h"

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

        Shader sv, sf;
        success = sv.Compile(GL_VERTEX_SHADER, vertex);
        if (!success)
            return false;

        success = sf.Compile(GL_FRAGMENT_SHADER, fragment);
        if (!success)
            return false;

        m_handle = glCreateProgram();
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            SDL_LogError(0, "Failed to create program, OpenGL error %d", glError);
            this->Release();
            return false;
        }

        glAttachShader(m_handle, sv.GetHandle());
        glAttachShader(m_handle, sf.GetHandle());
        glLinkProgram(m_handle);
        glDetachShader(m_handle, sv.GetHandle());
        glDetachShader(m_handle, sf.GetHandle());

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

