#ifndef ENGINE_RHI_VULKANPIPELINEENUMS_INCLUDED
#define ENGINE_RHI_VULKANPIPELINEENUMS_INCLUDED

#include "Rhi/Pipeline/PipelineEnums.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    /**
     * @brief Convert a depth comparison enumeration to its Vulkan equivalent.
     *
     * The portable enumeration deliberately mirrors `VkCompareOp`'s value order,
     * which is why the conversion is a cast. This header is part of the Vulkan
     * backend only: nothing portable includes it, so
     * `Rhi/Pipeline/PipelineEnums.h` stays free of backend headers.
     */
    constexpr vk::CompareOp ToVkCompareOp(DSComparator comp) {
        return static_cast<vk::CompareOp>(static_cast<int>(comp));
    }
} // namespace Engine::Rhi

#endif // ENGINE_RHI_VULKANPIPELINEENUMS_INCLUDED
