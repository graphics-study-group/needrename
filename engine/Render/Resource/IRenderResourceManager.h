#ifndef RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED
#define RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED

#include "Core/guid.h"
#include "RenderResourceHandle.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {
    class RenderSystem;

    namespace RenderSystemState {
        /**
         * @brief CRTP base class for GPU render-resource managers.
         *
         * @tparam ResourceType GPU-side resource payload type managed by this manager.
         *
         * Design overview:
         * - Each render resource is referenced by a typed handle (`HandleType`) instead of raw pointer.
         * - Manager stores records in a slot array (`m_records`) and uses index + generation to validate handles.
         * - Resource payload may exist before it is fully GPU-ready; readiness is controlled by resource-specific logic.
         * - `Acquire`/`AcquireAsync` increase record refcount and trigger resource-specific readiness path.
         * - `Release` decreases refcount; when it reaches 0, deferred reclamation countdown starts.
         * - `TickFrame` performs countdown and final destruction (`OnDestroy`) after configured frame delay.
         * - Re-acquire during countdown cancels pending reclamation (countdown reset to -1).
         * - `CreateOrReuseFromAsset` deduplicates resources built from the same asset GUID.
         *
         * CRTP rationale:
         * - Common lifetime bookkeeping is centralized in this template.
         * - Resource-specific behavior is delegated to derived manager methods:
         *   `CreateFromAssetImpl`, `AcquireImpl`, `AcquireAsyncImpl`, `ReleaseImpl`,
         *   `IsReadyImpl`, `EnsureReadyImpl`, `OnDestroyImpl`.
         * - This avoids virtual dispatch in hot paths while keeping API uniform.
         */
        template <typename ResourceType>
        class IRenderResourceManager {
        public:
            using HandleType = typename ResourceTraits<ResourceType>::HandleType;
            using ManagerType = typename ResourceTraits<ResourceType>::ManagerType;

            /**
             * @brief Construct manager with owning render system context.
             */
            IRenderResourceManager(RenderSystem &system) : m_system(system) {
            }
            ~IRenderResourceManager() = default;

            /**
             * @brief Create a managed render resource from an existing payload instance.
             *
             * Initial record refcount is 1 for the returned handle owner.
             *
             * @param resource Unique payload object to be owned by manager.
             * @param deallocate_after_frames Frame countdown before final destroy after refcount reaches 0.
             * @return Newly allocated typed handle.
             */
            HandleType Create(std::unique_ptr<ResourceType> resource, uint32_t deallocate_after_frames = 3u);

            /**
             * @brief Create resource from asset GUID, or reuse existing live one for the same GUID.
             *
             * Deduplication map is maintained in `m_guid_to_handle`.
             *
             * @param guid Source asset GUID.
             * @param deallocate_after_frames Frame countdown before final destroy after refcount reaches 0.
             * @return Existing or newly created typed handle.
             */
            HandleType CreateOrReuseFromAsset(GUID guid, uint32_t deallocate_after_frames = 3u);

            /**
             * @brief Resolve a typed handle to payload pointer.
             * @param handle Handle to resolve.
             * @return Non-owning payload pointer, or nullptr if handle is invalid.
             */
            ResourceType *Resolve(const HandleType &handle);

            /**
             * @brief Check whether handle currently points to a live record.
             *
             * @param handle Handle to validate.
             * @return True if index/generation are valid and payload exists.
             */
            bool IsHandleValid(const HandleType &handle) const noexcept;

            /**
             * @brief Mark handle as acquired and ensure resource becomes ready (synchronous policy by impl).
             *
             * Calls derived `AcquireImpl`, increments refcount once per handle acquire state,
             * and cancels any pending deallocation countdown.
             *
             * @param handle Handle to acquire.
             */
            void Acquire(HandleType &handle);

            /**
             * @brief Mark handle as acquired and request asynchronous readiness path.
             *
             * Calls derived `AcquireAsyncImpl`, increments refcount once per handle acquire state,
             * and cancels any pending deallocation countdown.
             *
             * @param handle Handle to acquire.
             */
            void AcquireAsync(HandleType &handle);

            /**
             * @brief Release a previously acquired handle.
             *
             * Calls derived `ReleaseImpl`, decrements refcount when handle is in acquired state.
             * If refcount becomes 0, starts deferred destroy countdown.
             *
             * @param handle Handle to release.
             */
            void Release(HandleType &handle);

            /**
             * @brief Query whether resource is ready for direct rendering usage.
             * @param handle Target handle.
             * @return True if ready, otherwise false.
             */
            bool IsReady(const HandleType &handle) const noexcept;

            /**
             * @brief Ensure resource reaches ready state.
             * @param handle Target handle.
             */
            void EnsureReady(HandleType &handle);

            /**
             * @brief Final destruction callback for a record about to be reclaimed.
             *
             * Derived implementation should release resource-specific GPU/CPU side dependencies.
             *
             * @param handle Target handle.
             */
            void OnDestroy(HandleType &handle);

            /**
             * @brief Advance one frame for deferred reclamation bookkeeping.
             *
             * For every record with `pending_deallocation_countdown >= 0`, countdown is decreased.
             * When it reaches 0, `OnDestroy` is called, payload is reset, and slot is recycled.
             */
            void TickFrame();

        protected:
            RenderSystem &m_system;

            /**
             * @brief Internal storage node for one managed render resource.
             */
            struct ResourceRecord {
                /// Slot generation to detect stale handle reuse.
                uint32_t generation{1};
                /// Number of currently acquired handle owners.
                uint32_t refcount{0};
                /// Remaining frames before destroy; -1 means no pending deallocation.
                int32_t pending_deallocation_countdown{-1};
                /// Frame delay configured when deallocation countdown starts.
                uint32_t deallocate_after_frames{3};

                /// Owned payload instance.
                std::unique_ptr<ResourceType> payload{};
            };

            /// Dense slot storage for payload records.
            std::vector<ResourceRecord> m_records{};
            /// Recycled slot indices available for future create operations.
            std::vector<uint32_t> m_free_indices{};
            /// Asset GUID to live handle mapping for create-or-reuse semantics.
            std::unordered_map<GUID, HandleType> m_guid_to_handle{};
        };
    } // namespace RenderSystemState
} // namespace Engine

#include "IRenderResourceManager.inl"

#endif // RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED
