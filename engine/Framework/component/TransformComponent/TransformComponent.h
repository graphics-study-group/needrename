#ifndef FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED

#include <Core/Math/Transform.h>
#include <Framework/component/Component.h>
#include <Reflection/macros.h>
#include <glm.hpp>

namespace Engine {
    class Transform;
    class GameObject;

    class REFL_SER_CLASS(REFL_WHITELIST) TransformComponent : public Component {
        REFL_SER_BODY(TransformComponent)
    public:
        REFL_ENABLE TransformComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~TransformComponent();

        void Tick() override;

        void SetTransform(const Transform &transform);

        /// @brief Get the transform that transform local coordinate to parent local coordinate
        /// @return Transform
        const Transform &GetTransform() const;

        /// @brief Get the transform that transform local coordinate to parent local coordinate
        /// @return Transform
        Transform &GetTransformRef();

    public:
        REFL_SER_ENABLE Transform m_transform{};
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED
