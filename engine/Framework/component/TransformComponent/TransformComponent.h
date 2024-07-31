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

        /// @brief Get the transform that transform local coordinate to parent local coordinate
        /// @return Transform
        const Transform& GetTransform() const;
    
        /// @brief Get the transform that transform local coordinate to parent local coordinate
        /// @return Transform
        Transform& GetTransform();
        
    protected:
        Transform m_transform {};
    };
}

#endif // FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED
