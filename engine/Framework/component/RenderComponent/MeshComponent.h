#ifndef MESHCOMPONENT_H
#define MESHCOMPONENT_H

#include <memory>
#include <vector>
#include <GLAD/glad.h>
#include "Render/Material.h"
#include "Framework/component/RenderComponent/RendererComponent.h"

namespace Engine
{
    class Component;
    class GameObject;

    class MeshComponent : public RendererComponent
    {
    public:
        MeshComponent(std::shared_ptr<Material> mat, std::weak_ptr<GameObject> gameObject);
        virtual ~MeshComponent();

        // TODO: tick for animation
        virtual void Tick(float dt) override;
        virtual void Draw() override;

        bool ReadAndFlatten(const char * filename);
        // TODO: set resources: mesh model, texture, shader 
    protected:
        std::vector <float> m_position;
        std::vector <float> m_uv;

        GLuint m_VAO, m_VBO[2];

        /// @brief Setup VAO and VBOs for OpenGL rendering
        void SetupVertices();
    };
}
#endif // MESHCOMPONENT_H