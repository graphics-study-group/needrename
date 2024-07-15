#include "SinglePassMaterial.h"
#include <stdexcept>
#include "RenderSystem.h"
#include "Framework/component/RenderComponent/RendererComponent.h"

namespace Engine {
    SinglePassMaterial::SinglePassMaterial (
        std::shared_ptr<RenderSystem> system, 
        const char * vertex, 
        const char * fragment
    ) : Material(system) {
        GLenum glError;

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertex, nullptr);
        glCompileShader(vertexShader);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            throw std::runtime_error("Cannot create vertex shader");
        }

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragment, nullptr);
        glCompileShader(fragmentShader);
        glError = glGetError();
        if (glError != GL_NO_ERROR) {
            throw std::runtime_error("Cannot create fragment shader");
        }

        m_shaderProgram = glCreateProgram();
        glAttachShader(m_shaderProgram, vertexShader);
        glAttachShader(m_shaderProgram, fragmentShader);
        glLinkProgram(m_shaderProgram);
        glError = glGetError();

        if (glError != GL_NO_ERROR) {
            throw std::runtime_error("Cannot link shader program");
        }

        glDetachShader(m_shaderProgram, vertexShader);
        glDetachShader(m_shaderProgram, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    SinglePassMaterial::~SinglePassMaterial () {
        if(m_shaderProgram)
            glDeleteProgram(m_shaderProgram);
    }
    
    void SinglePassMaterial::PrepareDraw()
    {
        glUseProgram(m_shaderProgram);
    }
};
