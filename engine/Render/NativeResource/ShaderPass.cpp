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
        assert(this->IsNativeValid() && "Trying to use unlinked program.");
        glUseProgram(m_handle);
    }

    ShaderPass::~ShaderPass()
    {
        this->Release();
    }

    bool ShaderPass::IsNativeValid() const noexcept
    {
        return glIsProgram(m_handle);
    }

    void ShaderPass::Release() noexcept
    {
        if (this->IsNativeValid()) {
            glDeleteProgram(m_handle);
        }
        this->m_handle = 0;
    }

    bool ShaderPass::Compile(const char *vertex, const char *fragment)
    {
        assert((!this->IsNativeValid()) && "Re-compiling a shader pass program.");
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
        assert(this->IsNativeValid() && "Trying to get uniform location from uncompiled program.");

        return glGetUniformLocation(m_handle, name);
    }

    bool ShaderPass::SetUniformInteger(GLint location, GLint value) noexcept
    {
        assert(!glGetError());

        glUniform1i(location, value);
        GLenum err = glGetError();
        if (err) {
            SDL_LogError(0, "Fail to set uniform location %d, OpenGL error: %d", location, err);
            return false;
        }
        return true;
    }

    bool ShaderPass::SetUniformIntegerVector(GLint location, const std::vector <GLint> & vector) noexcept
    {
        assert(!glGetError());

        switch(vector.size()) {
            case 1:
                glUniform1i(location, vector[0]);
                break;
            case 2:
                glUniform2i(location, vector[0], vector[1]);
                break;
            case 3:
                glUniform3i(location, vector[0], vector[1], vector[2]);
                break;
            case 4:
                glUniform4i(location, vector[0], vector[1], vector[2], vector[3]);
                break;
            default:
                SDL_LogError(0, "Exotic vector size %lld for uniform location %d.", vector.size(), location);
                return false;
        }
        GLenum err = glGetError();
        if (err) {
            SDL_LogError(0, "Fail to set uniform location %d, OpenGL error: %d", location, err);
            return false;
        }
        return true;
    }

    GLint ShaderPass::GetAttribute(const char *name) const noexcept
    {
        assert(this->IsNativeValid() && "Trying to get attribute location from uncompiled program.");
        return glGetAttribLocation(m_handle, name);
    }

} // namespace Engine
