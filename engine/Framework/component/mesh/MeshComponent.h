#ifndef MESHCOMPONENT_H
#define MESHCOMPONENT_H

#include <memory>
#include "Framework/component/Component.h"

namespace Engine
{
    class Component;
    class GameObject;

    class MeshComponent : public Component
    {
    public:
        MeshComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject) {}
        virtual ~MeshComponent();

        // TODO: tick for animation
        virtual void tick(float dt) override;

        // TODO: set resources: mesh model, texture, shader 
    };
}
#endif // MESHCOMPONENT_H