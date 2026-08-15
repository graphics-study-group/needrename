#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_LIGHTCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_LIGHTCOMPONENT_INCLUDED

#include "Framework/framework_export.h"
#include <AnnoRefl/macros.h>
#include <AnnoRefl/serialization_glm.h>
#include <Framework/Component/Component.h>
#include <glm.hpp>

namespace Engine {
    class Camera;

    enum REFL_SER_CLASS() LightType {
        Directional,
        Point,
        Spot
    };

    /// @brief A perspective camera component
    class FRAMEWORK_API REFL_SER_CLASS(REFL_WHITELIST) LightComponent : public Component {
        REFL_SER_BODY_OVERRIDE(LightComponent)
    public:
        REFL_ENABLE LightComponent(const GameObject &parent);
        virtual ~LightComponent() = default;

    public:
        REFL_SER_ENABLE glm::vec3 m_color{1.0f, 1.0f, 1.0f};
        REFL_SER_ENABLE float m_intensity{5.0f};
        REFL_SER_ENABLE LightType m_type{LightType::Directional};
        REFL_SER_ENABLE bool m_cast_shadow{true};
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_LIGHTCOMPONENT_INCLUDED
