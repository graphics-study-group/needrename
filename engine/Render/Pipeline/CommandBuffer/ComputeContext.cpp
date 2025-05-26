#include "ComputeContext.h"

namespace Engine {
    void ComputeContext::UseImage(vk::Image img, ImageAccessType currentAccess, ImageAccessType previousAccess) noexcept
    {
    }
    void ComputeContext::UseImage(vk::Image img, ImageComputeAccessType currentAccess, ImageAccessType previousAccess) noexcept
    {
    }
}
