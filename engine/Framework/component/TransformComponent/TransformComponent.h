#ifndef FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED

#include <glm.hpp>
#include "Core/Math/Transform.h"
#include "Framework/component/Component.h"

namespace Engine
{
    class Component;

    class TransformComponent : public Component
    {
    public:
        TransformComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~TransformComponent();

        void Tick(float dt) override;

        void SetTransform(const Transform & transform);
        const Transform& GetTransform() const;
        Transform& GetTransform();

        Transform GetWorldTransform() const;

    protected:
        Transform m_transform {};
    };
}

#endif // FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED
