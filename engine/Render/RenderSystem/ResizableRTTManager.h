#ifndef RENDER_RENDERSYSTEM_RESIZABLERTTMANAGER
#define RENDER_RENDERSYSTEM_RESIZABLERTTMANAGER

#include <cstdint>
#include <memory>

#include "Render/Memory/RenderTargetTexture.h"
#include <string>

namespace Engine {
    struct RRTTHandle;

    namespace RenderSystemState {
        enum class RRTTHandleEnum : uint32_t {};

        /**
         * @brief A manager class for resizable render target textures.
         * 
         * This manager facilitates management of render target textures whose sizes
         * are determined externally such as swapchain images.
         */
        class ResizableRTTManager {

            RenderSystem & m_system;
            struct impl;
            std::unique_ptr <impl> pimpl;

        public:

            ResizableRTTManager(RenderSystem & system);
            ~ResizableRTTManager();

            /**
             * @brief Request a resizable render target texture.
             * 
             * The render target texture will be lazily created. It will be
             * created by a `ResolveHandle` call, or a `RebuildCacheImmediately`
             * call.
             * 
             * @param description Description of the render target texture.
             * Dimension parameters (i.e. width and height) are ignored.
             * @param width_factor,height_factor Relative size of the RTT to
             * the reference size.
             */
            [[nodiscard]]
            RRTTHandle RequestRTT(
                RenderTargetTexture::RenderTargetTextureDesc description,
                RenderTargetTexture::SamplerDesc sampler_description,
                float width_factor = 1.0f,
                float height_factor = 1.0f,
                const std::string & name = ""
            );

            /**
             * @brief Set the reference size.
             * 
             * Invalidates all created RTTs. These RTTs will be recreated lazily
             * when resolved on the next time.
             */
            void SetReferenceSize(uint32_t width, uint32_t height) noexcept;

            /**
             * @brief Remove all caches.
             */
            void RemoveAllCache() noexcept;

            /**
             * @brief Rebuild all invalidated or uncreated RTTs eagerly.
             * 
             * Textures that are already created will not be modified. If you
             * want to flush caches, call `RemoveAllCache()` before.
             */
            void RebuildCacheImmediately();

            /**
             * @brief Resolve a resizable RTT handle.
             * @exception Throws `std::invalid_argument` if the handle is not
             * created by this manager.
             */
            RenderTargetTexture & ResolveHandle(RRTTHandleEnum handle);

            /**
             * @brief Resolve a resizable RTT handle.
             * 
             * The RTT will be created if not created yet.
             * 
             * @return Pointer to the RTT, or nullptr if it cannot be resolved.
             */
            RenderTargetTexture * ResolveHandleToPtr(
                RRTTHandleEnum handle
            ) noexcept;

            /**
             * @brief Get the description of a resizable RTT via handle.
             * @exception Throws `std::invalid_argument` if the handle is not
             * created by this manager.
             */
            const RenderTargetTexture::RenderTargetTextureDesc &
            GetTextureDescription(RRTTHandleEnum handle) const;

            /**
             * @brief Release a RTT, so that it will no longer be managed by
             * the manager.
             * 
             * Typically this method is not needed, as render target texture
             * should only be created once and never destroyed until program
             * termination.
             * 
             * @return a unique_ptr to the current render target texture if it
             * exists. Discard this pointer to destory the texture.
             */
            std::unique_ptr <RenderTargetTexture> ReleaseRTT(
                RRTTHandleEnum handle
            ) noexcept;
        };
    }

    /**
     * @brief Handle to a Resizable RTT, associated with a manager class.
     */
    struct RRTTHandle {
        std::reference_wrapper <RenderSystemState::ResizableRTTManager> manager;
        RenderSystemState::RRTTHandleEnum handle;

        /**
         * @brief See `ResizableRTTManager::ResolveHandle()`
         * 
         * Refrain from caching this result.
         */
        auto & Resolve() {
            return manager.get().ResolveHandle(handle);
        }

        /**
         * @brief See `ResizableRTTManager::Release()`.
         */
        auto Release() noexcept {
            return manager.get().ReleaseRTT(handle);
        }
    };
    
}

#endif // RENDER_RENDERSYSTEM_RESIZABLERTTMANAGER
