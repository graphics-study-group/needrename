#ifndef TESTTRIANGLERENDERERCOMPONENT_H
#define TESTTRIANGLERENDERERCOMPONENT_H

#include <memory>
#include <GLAD/glad.h>
#include "RendererComponent.h"

namespace Engine
{
    class GCoameObject;

    class TestTriangleRendererComponent final : public RendererComponent
    {
    public:
        TestTriangleRendererComponent(std::shared_ptr<Material> material, std::weak_ptr<GameObject> gameObject) 
            : RendererComponent(material, gameObject) {
            float v[] = {
                -0.5f, -0.5f, 0.0f, // 左下角
                0.5f, -0.5f, 0.0f, // 右下角
                0.0f,  0.5f, 0.0f  // 顶部
            };

            GLenum glError;

            glGenVertexArrays(1, &m_VAO);
            glGenBuffers(1, &m_VBO);

            glBindVertexArray(m_VAO);
            glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);

            glError = glGetError();
            if(glError != GL_NO_ERROR) {
                throw std::runtime_error("Failed to allocated VAO and VBO");
            }
        }
        virtual ~TestTriangleRendererComponent() {
            if(m_VAO)
                glDeleteVertexArrays(1, &m_VAO);
            if(m_VBO)
                glDeleteBuffers(1, &m_VBO);
        };

        virtual void Tick(float dt) {};
        virtual void Draw(/*Context*/) override {
            GLenum glError;
            m_material->PrepareDraw(/*Context, Transform, etc.*/);

            glBindVertexArray(m_VAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            
            glError = glGetError();
            if(glError != GL_NO_ERROR) {
                throw std::runtime_error("Cannot draw VAO.");
            }
        };

    protected:
        GLuint m_VAO, m_VBO;
        std::weak_ptr<GameObject> m_parentGameObject;
    };
}
#endif // TESTTRIANGLERENDERERCOMPONENT_H