#ifndef RENDER_RENDERER_RUNTIMERENDERER_INCLUDED
#define RENDER_RENDERER_RUNTIMERENDERER_INCLUDED

#include "Render/Renderer/VertexAttribute.h"
#include "Render/Resource/RenderResourceManager.h"

#include <cstdint>
#include <vector>

namespace Engine {
    class DeviceBuffer;

    /**
     * @brief Runtime renderer object registered into RendererManager.
     *
     * RuntimeRenderer owns resource handles and directly exposes geometry data
     * required by draw calls.
     */
    class RuntimeRenderer {
    public:
        static constexpr uint32_t MATERIAL_RESOURCE_SLOT = 0;
        static constexpr uint32_t MESH_RESOURCE_SLOT = 1;

        struct BufferBindingInfo {
            const DeviceBuffer *buffer;
            size_t offset;
            size_t size;
        };

    private:
        std::vector<RenderSystemState::RenderResourceHandle> m_resource_handles{};
        uint32_t m_layer{0xFFFFFFFF};
        bool m_cast_shadow{false};
        bool m_is_eagerly_loaded{false};

    protected:
        RuntimeRenderer(uint32_t layer, bool cast_shadow, bool eagerly_loaded) noexcept;

        void SetResourceHandle(uint32_t slot, RenderSystemState::RenderResourceHandle handle);

    public:
        virtual ~RuntimeRenderer() noexcept = default;

        RuntimeRenderer(const RuntimeRenderer &) = delete;
        RuntimeRenderer &operator=(const RuntimeRenderer &) = delete;

        uint32_t GetLayer() const noexcept;
        bool CastShadow() const noexcept;
        bool IsEagerlyLoaded() const noexcept;

        RenderSystemState::RenderResourceHandle GetResourceHandle(uint32_t slot) const noexcept;
        const std::vector<RenderSystemState::RenderResourceHandle> &GetResourceHandles() const noexcept;

        virtual bool IsReady() const noexcept = 0;
        virtual uint32_t GetIndexCount() const noexcept = 0;
        virtual uint32_t GetVertexAttributeCount() const noexcept = 0;
        virtual VertexAttribute GetVertexAttributeFormat() const noexcept = 0;
        virtual void FillVertexAttributeBufferBindings(std::vector<BufferBindingInfo> &bindings) const noexcept = 0;
        virtual BufferBindingInfo GetIndexBufferBinding() const noexcept = 0;

        std::vector<BufferBindingInfo> GetVertexAttributeBufferBindings() const noexcept {
            std::vector<BufferBindingInfo> ret;
            FillVertexAttributeBufferBindings(ret);
            return ret;
        }

        virtual bool IsResourcesReady(RenderSystemState::RenderResourceManager &resource_manager) const noexcept = 0;
    };
} // namespace Engine

#endif // RENDER_RENDERER_RUNTIMERENDERER_INCLUDED
