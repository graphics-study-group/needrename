#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED

#include "Framework/component/Component.h"
#include <vector>

namespace Engine
{
    class Material;

    class RendererComponent : public Component
    {
    public:
        RendererComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~RendererComponent() = default;

        virtual void Tick(float dt);
        virtual void Draw(/*Context*/) = 0;

    protected:
        std::vector<std::shared_ptr<Material>> m_materials;
        std::weak_ptr <GameObject> m_parentGameObject;
    };
}
#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
