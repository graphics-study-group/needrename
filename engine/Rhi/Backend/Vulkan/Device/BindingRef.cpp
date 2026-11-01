#include "Rhi/Device/BindingRef.h"

#include <atomic>

namespace Engine::Rhi {
    namespace {
        /// @brief The only identity source. Starts at one so that zero stays
        /// the invalid identity.
        std::atomic<uint64_t> g_next_vulkan_identity{1};
    } // namespace

    VulkanIdentity MintVulkanIdentity() noexcept {
        return VulkanIdentity{g_next_vulkan_identity.fetch_add(1, std::memory_order_relaxed)};
    }
} // namespace Engine::Rhi
