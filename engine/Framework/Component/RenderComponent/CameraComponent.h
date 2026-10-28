#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED

#include "Framework/framework_export.h"
#include <AnnoRefl/macros.h>
#include <Framework/Component/Component.h>
#include <glm.hpp>

namespace Engine {
    class Camera;
    /// @brief A perspective camera component
    class FRAMEWORK_API REFL_SER_CLASS(REFL_WHITELIST) CameraComponent : public Component {
        REFL_SER_BODY_OVERRIDE(CameraComponent)
    public:
        REFL_ENABLE CameraComponent(const GameObject &parent);
        virtual ~CameraComponent() = default;

        virtual void Tick() override;

        /**
         * @brief Recompute the view matrix from the parent's current world transform.
         *
         * `Tick()` calls this while the scene is stepped, and
         * `WorldSystem::UpdateRendererData` calls it every rendered frame, so a
         * host that renders without stepping the scene (the editor before the game
         * is started) still draws and fits shadows from the camera's current pose.
         */
        void UpdateViewMatrix();

    public:
        REFL_SER_ENABLE std::shared_ptr<Camera> m_camera{};

    protected:
        void UpdateProjectionMatrix();
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED
