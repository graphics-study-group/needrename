#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_TESTTRIANGLERENDERERCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_TESTTRIANGLERENDERERCOMPONENT_INCLUDED

#include <memory>
#include <GLAD/glad.h>
#include "RendererComponent.h"
#include "Framework/component/RenderComponent/CameraComponent.h"

namespace Engine
{
    class GCoameObject;

    class TestTriangleRendererComponent final : public RendererComponent
    {
    public:
        TestTriangleRendererComponent(std::shared_ptr<Material> material, std::weak_ptr<GameObject> gameObject) 
            : RendererComponent(gameObject) {
            m_materials.push_back(material);

            float clip_space_coordinate[] = {
                -0.8f,  0.8f, 0.0f,
                 0.8f, -0.8f, 0.0f,
                -0.8f, -0.8f, 0.0f,
                 0.8f,  0.8f, 0.0f,
                 0.8f, -0.8f, 0.0f,
                -0.8f,  0.8f, 0.0f
            };

            float uv[] = {
                0.0f,1.0f,1.0f,0.0f,0.0f,0.0f,
                1.0f,1.0f,1.0f,0.0f,0.0f,1.0f
            };

            GLenum glError;

            glGenVertexArrays(1, &m_VAO);
            glGenBuffers(2, m_VBO);
            glBindVertexArray(m_VAO);
            
            // Assign clip space coordinate
            glBindBuffer(GL_ARRAY_BUFFER, m_VBO[0]);
            glBufferData(GL_ARRAY_BUFFER, sizeof(clip_space_coordinate), clip_space_coordinate, GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            //                    ^
            //                    This index can be acquired by glGetAttribLocation
            //                    or specified directly with layout directive in shader.

            // Assign UV coordinate
            glBindBuffer(GL_ARRAY_BUFFER, m_VBO[1]);
            glBufferData(GL_ARRAY_BUFFER, sizeof(uv), uv, GL_STATIC_DRAW);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);

            glError = glGetError();
            if(glError != GL_NO_ERROR) {
                throw std::runtime_error("Failed to allocate VAO and VBO");
            }
        }
        virtual ~TestTriangleRendererComponent() {
            if(m_VAO) {
                glDeleteVertexArrays(1, &m_VAO);
                glDeleteBuffers(2, m_VBO);
            }
        };

        virtual void Tick(float dt) {};
        virtual void Draw(CameraContext context) override {
            GLenum glError;
            m_materials[0]->PrepareDraw(context, this->CreateContext());

            glBindVertexArray(m_VAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            
            glError = glGetError();
            if(glError != GL_NO_ERROR) {
                throw std::runtime_error("Cannot draw VAO.");
            }
        };

    protected:
        GLuint m_VAO, m_VBO[2];
        // std::weak_ptr<GameObject> m_parentGameObject;
    };
}
#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_TESTTRIANGLERENDERERCOMPONENT_INCLUDED
