#include "Rhi/Device/ResourceRef.h"

#include <atomic>

namespace Engine::Rhi {
    namespace {
        /// @brief The only identity source. Starts at one so that zero stays
        /// the invalid identity.
        std::atomic<uint64_t> g_next_object_id{1};
    } // namespace

    ObjectId MintObjectId() noexcept {
        return ObjectId{g_next_object_id.fetch_add(1, std::memory_order_relaxed)};
    }
} // namespace Engine::Rhi
