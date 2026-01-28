#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_LIGHTCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_LIGHTCOMPONENT_INCLUDED

#include <Framework/component/Component.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_glm.h>
#include <glm.hpp>

namespace Engine {
    class Camera;

    enum REFL_SER_CLASS() LightType {
        Directional,
        Point,
        Spot
    };

    /// @brief A perspective camera component
    class REFL_SER_CLASS(REFL_WHITELIST) LightComponent : public Component {
        REFL_SER_BODY(LightComponent)
    public:
        REFL_ENABLE LightComponent(ObjectHandle gameObject);
        virtual ~LightComponent() = default;

    public:
        REFL_SER_ENABLE glm::vec3 m_color{1.0f, 1.0f, 1.0f};
        REFL_SER_ENABLE float m_intensity{10.0f};
        REFL_SER_ENABLE LightType m_type{LightType::Directional};
        REFL_SER_ENABLE bool m_cast_shadow{true};
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_LIGHTCOMPONENT_INCLUDED
