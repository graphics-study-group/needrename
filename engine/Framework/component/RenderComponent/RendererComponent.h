#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED

#include <Core/Math/Transform.h>
#include <Framework/component/Component.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_vector.h>
#include <vector>

namespace Engine {
    class AssetRef;
    class MaterialInstance;
    class RenderSystem;

    class REFL_SER_CLASS(REFL_WHITELIST) RendererComponent : public Component {
        REFL_SER_BODY(RendererComponent)
    protected:
        using RendererHandle = uint32_t;
        
        std::vector<std::shared_ptr<MaterialInstance>> m_materials{};
        std::weak_ptr<RenderSystem> m_system{};
        RendererHandle m_renderer_handle{};

    public:
        REFL_ENABLE RendererComponent(GameObject *parent);
        virtual ~RendererComponent() = default;

        /// @brief Get the transform which transforms local coordinate
        /// to world coordinate (i.e. the model matrix)
        /// @return Transform
        virtual Transform GetWorldTransform() const;
        
        virtual void UnregisterFromRenderSystem();

        virtual void RenderInit();
        virtual void Tick() override;

        std::shared_ptr<MaterialInstance> GetMaterial(uint32_t slot) const;
        auto GetMaterials() -> decltype(m_materials) &;
        auto GetMaterials() const -> const decltype(m_materials) &;

        REFL_SER_ENABLE std::vector<std::shared_ptr<AssetRef>> m_material_assets{};
        /// @brief Is this renderer eagerly loaded onto the GPU instead of loaded on use?
        REFL_SER_ENABLE bool m_is_eagerly_loaded{false};
        /// @brief Do this renderer cast shadow (viz. rendered onto shadowmaps)?
        REFL_SER_ENABLE bool m_cast_shadow{false};
        /// @brief Bits of the layers of the renderer (eg. opaque, transparent or HUD).
        REFL_SER_ENABLE uint32_t m_layer{0xFFFFFFFF};
        /// @brief Currently unused.
        REFL_SER_ENABLE uint32_t m_priority{0};
    };
} // namespace Engine
#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
