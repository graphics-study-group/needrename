#include "Material.h"
#include "RenderSystem.h"
#include "Framework/component/RenderComponent/RendererComponent.h"

namespace Engine {
    Material::Material (std::shared_ptr<RenderSystem> system, const char * vertex, const char * fragment) {
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

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    Material::~Material () {
        if(m_shaderProgram)
            glDeleteProgram(m_shaderProgram);
    }
    
    void Material::PrepareDraw()
    {
        glUseProgram(m_shaderProgram);
    }
};
