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

        virtual void Tick(float dt);
        virtual void Draw(/*Context*/) = 0;

    protected:
        std::shared_ptr <Material> m_material;
        std::weak_ptr <GameObject> m_parentGameObject;
    };
}
#endif // RENDERERCOMPONENT_H