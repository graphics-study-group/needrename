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
        /// @brief Distance from the camera that this light's shadows reach, in world units.
        /// @details Clamped to a positive value when the cascades are fitted.
        REFL_SER_ENABLE float m_shadow_distance{100.0f};
        /// @brief Number of cascades this light's shadow map is divided into.
        /// @details Clamped to 1..4 when the cascades are fitted.
        REFL_SER_ENABLE uint32_t m_cascade_count{4};
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_LIGHTCOMPONENT_INCLUDED
