#include "PerModelConstants.h"

namespace Engine::ConstantData {
    vk::PushConstantRange PerModelConstantPushConstant::GetPushConstantRange() {
        return vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, PUSH_RANGE_OFFSET, PUSH_RANGE_SIZE};
    }
} // namespace Engine::ConstantData
