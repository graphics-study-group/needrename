#ifndef RENDERERCOMPONENT_H
#define RENDERERCOMPONENT_H

#include "Framework/component/Component.h"

namespace Engine
{
    class Material;

    class RendererComponent : public Component
    {
    public:
        RendererComponent(std::shared_ptr<Material> material, std::weak_ptr<GameObject> gameObject);
        virtual ~RendererComponent() = default;

        virtual void tick(float dt);
        virtual void draw() = 0;

    protected:
        std::weak_ptr <Material> m_material;
        std::weak_ptr <GameObject> m_parentGameObject;
    };
}
#endif // RENDERERCOMPONENT_H