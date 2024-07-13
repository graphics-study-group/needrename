#ifndef MESHCOMPONENT_H
#define MESHCOMPONENT_H

#include <memory>
#include "Render/Material.h"
#include "Framework/component/RenderComponent/RendererComponent.h"

namespace Engine
{
    class Component;
    class GameObject;

    class MeshComponent : public RendererComponent
    {
    public:
        MeshComponent(std::shared_ptr<Material> mat, std::weak_ptr<GameObject> gameObject) : RendererComponent(mat, gameObject) {}
        virtual ~MeshComponent();

        // TODO: tick for animation
        virtual void tick(float dt) override;
        virtual void draw() override;

        // TODO: set resources: mesh model, texture, shader 
    };
}
#endif // MESHCOMPONENT_H