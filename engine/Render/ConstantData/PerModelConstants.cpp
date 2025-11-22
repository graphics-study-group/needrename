#include "PerModelConstants.h"
#include <vulkan/vulkan.hpp>

namespace Engine::ConstantData {
    vk::PushConstantRange PerModelConstantPushConstant::GetPushConstantRange() {
        return vk::PushConstantRange{vk::ShaderStageFlagBits::eAll, PUSH_RANGE_OFFSET, PUSH_RANGE_SIZE};
    }
} // namespace Engine::ConstantData
