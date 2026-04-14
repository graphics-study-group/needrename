#ifndef RENDER_RESOURCE_RENDERRESOURCEMANAGER_INCLUDED
#define RENDER_RESOURCE_RENDERRESOURCEMANAGER_INCLUDED

#include "Core/guid.h"

#include <memory>
#include <typeindex>
#include <utility>
#include <vector>

namespace Engine {
    class IVertexBasedRenderer;
    class MaterialInstance;
    class MaterialLibrary;
    class RenderSystem;
    class StaticMeshResource;

    namespace RenderSystemState {
        /**
         * @brief Opaque handle for resources managed by RenderResourceManager.
         *
         * A handle is valid only if index/generation/type_id matches a live record.
         */
        struct RenderResourceHandle {
            /// Index into resource record storage.
            uint32_t index{0xFFFFFFFFu};
            /// Generation used to detect stale handles.
            uint32_t generation{0};
            /// Runtime provider type id for safe resolve.
            std::type_index type_id{typeid(void)};

            /**
             * @brief Check whether this handle is structurally valid.
             */
            bool IsValid() const noexcept {
                return type_id != std::type_index(typeid(void)) && index != 0xFFFFFFFFu;
            }
        };

        /**
         * @brief Unified runtime manager for render resources used by draw path.
         *
         * This manager owns resource records, performs reference counting,
         * supports frame-delayed reclamation, and resolves typed payload pointers
         * from RenderResourceHandle.
         */
        class RenderResourceManager final {
        private:
            struct impl;
            RenderSystem &m_system;
            std::unique_ptr<impl> pimpl;

        public:
            /**
             * @brief Create manager bound to a RenderSystem instance.
             */
            RenderResourceManager(RenderSystem &system);
            ~RenderResourceManager();

            /**
             * @brief Acquire/create resource by type and GUID.
             */
            template <typename T>
            RenderResourceHandle Acquire(GUID guid) {
                return AcquireByType(typeid(T *), guid);
            }

            /**
             * @brief Resolve typed resource pointer from handle.
             */
            template <typename T>
            T *Resolve(RenderResourceHandle handle) const noexcept {
                return static_cast<T *>(ResolveByType(handle, typeid(T *)));
            }

            /**
             * @brief Ensure typed resource is ready for runtime use.
             */
            template <typename T>
            bool EnsureReady(RenderResourceHandle handle) {
                return EnsureReadyByType(handle, typeid(T *));
            }

            /**
             * @brief Decrease strong reference count of a resource handle.
             *
             * Actual destruction is delayed by frame-in-flight policy.
             */
            void Release(RenderResourceHandle handle);

            /**
             * @brief Advance one frame for deferred reclamation.
             */
            void TickFrame();

            /**
             * @brief Try to reuse an existing record known by a provider.
             *
             * Returns an invalid handle if the record is stale or type mismatched.
             */
            RenderResourceHandle TryReuseRecord(std::type_index type_id, uint32_t index) noexcept;

            /**
             * @brief Create a new resource record for a provider.
             */
            RenderResourceHandle CreateRecord(
                std::type_index type_id,
                GUID guid,
                std::shared_ptr<void> payload,
                std::vector<RenderResourceHandle> dependencies = {}
            );

            /**
             * @brief Resolve the stored payload for a provider after type validation.
             */
            void *ResolvePayload(RenderResourceHandle handle, std::type_index expected_type_id) const noexcept;

        private:
            RenderResourceHandle AcquireByType(std::type_index type_id, GUID guid);
            void *ResolveByType(RenderResourceHandle handle, std::type_index type_id) const noexcept;
            bool EnsureReadyByType(RenderResourceHandle handle, std::type_index type_id);
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_RENDERRESOURCEMANAGER_INCLUDED
