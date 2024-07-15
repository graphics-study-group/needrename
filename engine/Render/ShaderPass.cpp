#include "ShaderPass.h"
#include <format>
#include <cassert>
#include <stdexcept>

namespace Engine
{
    ShaderPass::ShaderPass() : m_shaderProgram(0) {}

    ShaderPass::~ShaderPass()
    {
        if (m_shaderProgram)
            glDeleteProgram(m_shaderProgram);
    }

    GLuint ShaderPass::GetProgram() const noexcept
    {
        return m_shaderProgram;
    }

    void ShaderPass::Use() const
    {
        assert(this->IsLinked() && "Trying to use unlinked program");
        glUseProgram(m_shaderProgram);
    }

    bool ShaderPass::IsLinked() const noexcept
    {
        return m_shaderProgram;
    }

    bool ShaderPass::Compile(const char *vertex, const char *fragment)
    {
        if (m_shaderProgram) {
            glDeleteProgram(m_shaderProgram);
            m_shaderProgram = 0;
        }

        GLenum glError = GL_NO_ERROR;

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        assert(glIsShader(vertexShader) == GL_TRUE);

        glShaderSource(vertexShader, 1, &vertex, nullptr);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            throw std::runtime_error(std::format("Failed to set vertex shader source, OpenGL error {}", (int)glError));
            return false;
        }

        glCompileShader(vertexShader);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            throw std::runtime_error(std::format("Failed to compile vertex shader, OpenGL error {}", (int)glError));
            return false;
        }

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        assert(glIsShader(fragmentShader) == GL_TRUE);

        glShaderSource(fragmentShader, 1, &fragment, nullptr);
        glCompileShader(fragmentShader);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            throw std::runtime_error(std::format("Failed to compile fragment shader, OpenGL error {}", (int)glError));
            return false;
        }

        m_shaderProgram = glCreateProgram();
        glAttachShader(m_shaderProgram, vertexShader);
        glAttachShader(m_shaderProgram, fragmentShader);
        glLinkProgram(m_shaderProgram);
        glError = glGetError();

        glDetachShader(m_shaderProgram, vertexShader);
        glDetachShader(m_shaderProgram, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        if (glError != GL_NO_ERROR) {
            m_shaderProgram = 0;
            throw std::runtime_error(std::format("Failed to link program, OpenGL error {}", (int)glError));
            return false;
        }
        return true;
    }

    GLuint ShaderPass::GetUniform(const char * name) const noexcept
    {
        static_assert(std::is_same_v<GLchar, char> == true, "GLchar and char should be the same type.");
        assert(this->IsLinked() && "Trying to get uniform location from uncompiled program.");

        return glGetUniformLocation(m_shaderProgram, name);
    }

} // namespace Engine

