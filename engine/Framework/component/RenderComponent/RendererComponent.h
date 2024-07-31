#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED

#include "Core/Math/Transform.h"
#include "Framework/component/Component.h"
#include <vector>

namespace Engine
{
    class Material;

    /// @brief A context storing renderer related information for rendering
    struct RendererContext{
        /// @brief model matrix
        glm::mat4 model_matrix;
    };

    class RendererComponent : public Component
    {
    public:
        RendererComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~RendererComponent() = default;

        /// @brief Get the transform which transforms local coordinate 
        /// to world coordinate (i.e. the model matrix)
        /// @return Transform
        Transform GetWorldTransform() const;

        /// @brief Get the context for this renderer,
        /// which should be passed to material draw calls.
        /// @return RendererContext
        virtual RendererContext CreateContext() const;

        virtual void Tick(float dt);
        virtual void Draw(/*Context*/) = 0;

    protected:
        std::vector<std::shared_ptr<Material>> m_materials{};
    };
}
#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
