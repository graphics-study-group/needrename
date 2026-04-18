#ifndef RENDER_RESOURCE_RENDERRESOURCEMANAGER_INCLUDED
#define RENDER_RESOURCE_RENDERRESOURCEMANAGER_INCLUDED

#include "Core/guid.h"

#include <memory>
#include <typeindex>
#include <utility>
#include <vector>

namespace Engine {
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
         *
         * High-level workflow:
         * - Caller asks Acquire<T>/AcquireAsync<T> with a GUID.
         * - Manager dispatches to provider by typeid(T*).
         * - Provider returns/reuses a RenderResourceHandle.
         * - Caller may poll IsReady<T> in async paths.
         * - Caller may call EnsureReady<T> to force immediate readiness.
         *
         * Handles are opaque tokens; payload ownership and type-specific
         * readiness logic stay inside each provider.
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
             *
             * This is the synchronous entry. Provider may immediately perform
             * heavyweight preparation according to its policy.
             *
             * @tparam T Resource payload type.
             * @param guid Asset GUID key used by provider lookup.
             * @return Opaque handle to resource record, invalid on failure.
             */
            template <typename T>
            RenderResourceHandle Acquire(GUID guid) {
                return AcquireByType(typeid(T *), guid);
            }

            /**
             * @brief Acquire/create resource by type and GUID asynchronously.
             *
             * This is the async-friendly entry. The returned handle may point
             * to a resource that is not yet ready for rendering.
             *
             * @tparam T Resource payload type.
             * @param guid Asset GUID key used by provider lookup.
             * @return Opaque handle to resource record, invalid on failure.
             */
            template <typename T>
            RenderResourceHandle AcquireAsync(GUID guid) {
                return AcquireAsyncByType(typeid(T *), guid);
            }

            /**
             * @brief Resolve typed resource pointer from handle.
             *
             * This call does not imply readiness. It only resolves payload
             * identity and type; caller should use IsReady/EnsureReady when
             * consuming async-acquired resources.
             *
             * @tparam T Resource payload type.
             * @param handle Resource handle.
             * @return Typed payload pointer, or nullptr when invalid/stale.
             */
            template <typename T>
            T *Resolve(RenderResourceHandle handle) const noexcept {
                return static_cast<T *>(ResolveByType(handle, typeid(T *)));
            }

            /**
             * @brief Check whether a typed resource is already ready.
             *
             * This query must be side-effect free from caller perspective.
             *
             * @tparam T Resource payload type.
             * @param handle Resource handle.
             * @return True when provider reports immediate consumable state.
             */
            template <typename T>
            bool IsReady(RenderResourceHandle handle) const noexcept {
                return IsReadyByType(handle, typeid(T *));
            }

            /**
             * @brief Ensure typed resource is ready for runtime use.
             *
             * This is a synchronous fallback entry that may force pending work
             * (for example, blocking asset completion or immediate GPU submit).
             *
             * @tparam T Resource payload type.
             * @param handle Resource handle.
             */
            template <typename T>
            void EnsureReady(RenderResourceHandle handle) {
                EnsureReadyByType(handle, typeid(T *));
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
             *
             * @param type_id Provider type id.
             * @param guid Resource key.
             * @param payload Shared payload owned by record.
             * @param dependencies Handles retained while this record is alive.
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
            RenderResourceHandle AcquireAsyncByType(std::type_index type_id, GUID guid);
            void *ResolveByType(RenderResourceHandle handle, std::type_index type_id) const noexcept;
            bool IsReadyByType(RenderResourceHandle handle, std::type_index type_id) const noexcept;
            void EnsureReadyByType(RenderResourceHandle handle, std::type_index type_id);
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_RENDERRESOURCEMANAGER_INCLUDED
