#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED

#include "Core/Math/Transform.h"
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

        /// @brief Get the transform which transforms local coordinate 
        /// to world coordinate (i.e. the model matrix)
        /// @return Transform
        Transform GetWorldTransform() const;

        virtual void Tick(float dt);
        virtual void Draw(/*Context*/) = 0;

    protected:
        std::vector<std::shared_ptr<Material>> m_materials;
    };
}
#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
