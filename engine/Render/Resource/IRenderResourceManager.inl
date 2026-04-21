#include "IRenderResourceManager.h"

namespace Engine::RenderSystemState {
    template <typename ResourceType>
    typename IRenderResourceManager<ResourceType>::HandleType IRenderResourceManager<ResourceType>::Create(
        std::unique_ptr<ResourceType> resource, uint32_t deallocate_after_frames
    ) {
        uint32_t index = 0;
        if (!m_free_indices.empty()) {
            index = m_free_indices.back();
            m_free_indices.pop_back();
        } else {
            index = static_cast<uint32_t>(m_records.size());
            m_records.emplace_back();
        }

        auto &record = m_records[index];
        auto generation = record.generation + 1;
        record = {};
        record.generation = generation;
        record.refcount = 1;
        record.pending_deallocation_countdown = -1;
        record.deallocate_after_frames = deallocate_after_frames;
        record.payload = std::move(resource);

        return HandleType{index, generation};
    }

    template <typename ResourceType>
    typename IRenderResourceManager<ResourceType>::HandleType IRenderResourceManager<
        ResourceType>::CreateOrReuseFromAsset(GUID guid, uint32_t deallocate_after_frames) {
        if (m_guid_to_handle.find(guid) != m_guid_to_handle.end()) {
            auto handle = m_guid_to_handle[guid];
            if (IsHandleValid(handle)) return handle;
            m_guid_to_handle.erase(guid);
        }
        auto handle = static_cast<ManagerType *>(this)->CreateFromAssetImpl(guid, deallocate_after_frames);
        m_guid_to_handle[guid] = handle;
        return handle;
    }

    template <typename ResourceType>
    ResourceType *IRenderResourceManager<ResourceType>::Resolve(HandleType handle) {
        if (!IsHandleValid(handle)) return nullptr;
        return m_records[handle.index].payload.get();
    }

    template <typename ResourceType>
    bool IRenderResourceManager<ResourceType>::IsHandleValid(HandleType handle) const noexcept {
        if (!handle.IsValid()) return false;
        if (handle.index >= m_records.size()) return false;

        const auto &record = m_records[handle.index];
        // Critical stale-handle guard: same index is insufficient when slots are recycled.
        if (record.generation != handle.generation) return false;
        if (record.payload == nullptr) return false;
        return true;
    }

    template <typename ResourceType>
    void IRenderResourceManager<ResourceType>::Acquire(HandleType handle) {
        static_cast<ManagerType *>(this)->AcquireImpl(handle);
        if (!handle.is_acquired) {
            auto &record = m_records[handle.index];
            record.refcount += 1;
            record.pending_deallocation_countdown = -1;
            handle.is_acquired = true;
        }
    }

    template <typename ResourceType>
    void IRenderResourceManager<ResourceType>::AcquireAsync(HandleType handle) {
        static_cast<ManagerType *>(this)->AcquireAsyncImpl(handle);
        if (!handle.is_acquired) {
            auto &record = m_records[handle.index];
            record.refcount += 1;
            record.pending_deallocation_countdown = -1;
            handle.is_acquired = true;
        }
    }

    template <typename ResourceType>
    void IRenderResourceManager<ResourceType>::Release(HandleType handle) {
        static_cast<ManagerType *>(this)->ReleaseImpl(handle);
        if (handle.is_acquired) {
            auto &record = m_records[handle.index];
            assert(record.refcount > 0);
            record.refcount -= 1;
            if (record.refcount == 0) {
                record.pending_deallocation_countdown = record.deallocate_after_frames;
            }
        }
        handle.is_acquired = false;
    }

    template <typename ResourceType>
    bool IRenderResourceManager<ResourceType>::IsReady(HandleType handle) const noexcept {
        return static_cast<const ManagerType *>(this)->IsReadyImpl(handle);
    }

    template <typename ResourceType>
    void IRenderResourceManager<ResourceType>::EnsureReady(HandleType handle) {
        static_cast<ManagerType *>(this)->EnsureReadyImpl(handle);
    }

    template <typename ResourceType>
    void IRenderResourceManager<ResourceType>::OnDestroy(HandleType handle) {
        static_cast<ManagerType *>(this)->OnDestroyImpl(handle);
    }

    template <typename ResourceType>
    void IRenderResourceManager<ResourceType>::TickFrame() {
        for (uint32_t i = 0; i < m_records.size(); ++i) {
            auto &record = m_records[i];
            if (record.payload == nullptr) continue;
            if (record.pending_deallocation_countdown < 0) continue;

            record.pending_deallocation_countdown -= 1;
            if (record.pending_deallocation_countdown > 0) continue;

            const auto handle = typename ManagerType::HandleType{i, record.generation};
            OnDestroy(handle);
            record.payload.reset();
            m_free_indices.push_back(i);
        }
    }
} // namespace Engine::RenderSystemState
