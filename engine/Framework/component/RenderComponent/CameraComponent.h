#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED

#include <glm.hpp>
#include <Framework/component/Component.h>
#include <Reflection/macros.h>

namespace Engine
{
    class Camera;
    /// @brief A perspective camera component
    class REFL_SER_CLASS(REFL_WHITELIST) CameraComponent : public Component {
        REFL_SER_BODY(CameraComponent)
    public:
        REFL_ENABLE CameraComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~CameraComponent() = default;

        virtual void Tick() override;

    public:
        REFL_SER_ENABLE std::shared_ptr<Camera> m_camera{};
    
    protected:
        void UpdateViewMatrix();
        void UpdateProjectionMatrix();
    };
} // namespace Engine


#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED
