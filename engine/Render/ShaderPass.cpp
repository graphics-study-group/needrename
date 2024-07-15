#include "ShaderPass.h"
#include <format>
#include <cassert>
#include <stdexcept>
#include <iostream>

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

    void ShaderPass::Use() const noexcept
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
        GLint success = GL_TRUE;

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            std::cerr << std::format("Failed to create vertex shader, OpenGL error {}", (int)glError) << std::endl;
            return false;
        }

        glShaderSource(vertexShader, 1, &vertex, nullptr);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            std::cerr << std::format("Failed to set vertex shader source, OpenGL error {}", (int)glError) << std::endl;
            return false;
        }

        glCompileShader(vertexShader);
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (success != GL_TRUE) {
            GLchar log[128];
            glGetShaderInfoLog(vertexShader, sizeof log, nullptr, log);
            std::cerr 
                << std::format("Failed to compile vertex shader, log: {}", log) 
                << std::endl;
            return false;
        }

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            std::cerr << std::format("Failed to create fragment shader, OpenGL error {}", (int)glError) << std::endl;
            return false;
        }

        glShaderSource(fragmentShader, 1, &fragment, nullptr);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            std::cerr 
                << std::format("Failed to set fragment shader source, OpenGL error {}", (int)glError) 
                << std::endl;
            return false;
        }

        glCompileShader(fragmentShader);
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (success != GL_TRUE) {
            GLchar log[128];
            glGetShaderInfoLog(fragmentShader, sizeof log, nullptr, log);
            std::cerr 
                << std::format("Failed to compile fragment shader, log: {}", log) 
                << std::endl ;
            return false;
        }

        m_shaderProgram = glCreateProgram();
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            std::cerr 
                << std::format("Failed to create program, OpenGL error {}", (int)glError) 
                << std::endl ;
            m_shaderProgram = 0;
            return false;
        }

        glAttachShader(m_shaderProgram, vertexShader);
        glAttachShader(m_shaderProgram, fragmentShader);
        glLinkProgram(m_shaderProgram);
        glDetachShader(m_shaderProgram, vertexShader);
        glDetachShader(m_shaderProgram, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &success);
        if (success != GL_TRUE) {
            GLchar log[128];
            glGetProgramInfoLog(m_shaderProgram, sizeof log, nullptr, log);
            std::cerr 
                << std::format("Failed to link program, log: {}", log) 
                << std::endl ;

            m_shaderProgram = 0;
            return false;
        }
        return true;
    }

    GLint ShaderPass::GetUniform(const char * name) const noexcept
    {
        static_assert(std::is_same_v<GLchar, char> == true, "GLchar and char should be the same type.");
        assert(this->IsLinked() && "Trying to get uniform location from uncompiled program.");

        return glGetUniformLocation(m_shaderProgram, name);
    }

} // namespace Engine

