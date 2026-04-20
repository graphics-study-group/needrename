#include "IRenderResourceProvider.h"

namespace Engine::RenderSystemState {
    template <typename ResourceType>
    typename IRenderResourceProvider<ResourceType>::HandleType IRenderResourceProvider<ResourceType>::Create(
        std::unique_ptr<ResourceType> resource
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
        auto generation = record.generation == 0 ? 1u : record.generation;
        record = {};
        record.generation = generation;
        record.refcount = 1;
        record.pending_deallocation_countdown = -1;
        record.payload = std::move(resource);

        return HandleType{index, generation};
    }

    template <typename ResourceType>
    typename IRenderResourceProvider<ResourceType>::HandleType IRenderResourceProvider<
        ResourceType>::CreateOrReuseFromAsset(GUID guid) {
        if (m_guid_to_handle.find(guid) != m_guid_to_handle.end()) {
            auto handle = m_guid_to_handle[guid];
            if (IsHandleValid(handle)) return handle;
            m_guid_to_handle.erase(guid);
        }
        return static_cast<ProviderType *>(this)->CreateFromAssetImpl(guid);
    }

    template <typename ResourceType>
    ResourceType *IRenderResourceProvider<ResourceType>::Resolve(HandleType handle) {
        if (!IsHandleValid(handle)) return nullptr;
        return m_records[handle.index].payload.get();
    }

    template <typename ResourceType>
    bool IRenderResourceProvider<ResourceType>::IsHandleValid(HandleType handle) const noexcept {
        if (!handle.IsValid()) return false;
        if (handle.index >= m_records.size()) return false;

        const auto &record = m_records[handle.index];
        // Critical stale-handle guard: same index is insufficient when slots are recycled.
        if (record.generation != handle.generation) return false;
        if (record.payload == nullptr) return false;
        return true;
    }

    template <typename ResourceType>
    void IRenderResourceProvider<ResourceType>::Acquire(HandleType handle) {
        static_cast<ProviderType *>(this)->AcquireImpl(handle);
    }

    template <typename ResourceType>
    void IRenderResourceProvider<ResourceType>::AcquireAsync(HandleType handle) {
        static_cast<ProviderType *>(this)->AcquireAsyncImpl(handle);
    }

    template <typename ResourceType>
    void IRenderResourceProvider<ResourceType>::Release(HandleType handle) {
        static_cast<ProviderType *>(this)->ReleaseImpl(handle);
    }

    template <typename ResourceType>
    bool IRenderResourceProvider<ResourceType>::IsReady(HandleType handle) const noexcept {
        return static_cast<const ProviderType *>(this)->IsReadyImpl(handle);
    }

    template <typename ResourceType>
    void IRenderResourceProvider<ResourceType>::EnsureReady(HandleType handle) {
        static_cast<ProviderType *>(this)->EnsureReadyImpl(handle);
    }

    template <typename ResourceType>
    void IRenderResourceProvider<ResourceType>::OnDestroy(HandleType handle) {
        static_cast<ProviderType *>(this)->OnDestroyImpl(handle);
    }

    template <typename ResourceType>
    void IRenderResourceProvider<ResourceType>::TickFrame() {
        for (uint32_t i = 0; i < m_records.size(); ++i) {
            auto &record = m_records[i];
            if (record.payload == nullptr) continue;
            if (record.pending_deallocation_countdown < 0) continue;

            record.pending_deallocation_countdown -= 1;
            if (record.pending_deallocation_countdown > 0) continue;

            const auto handle = typename ProviderType::HandleType{i, record.generation};
            OnDestroy(handle);
            record.payload.reset();
        }
    }
} // namespace Engine::RenderSystemState
