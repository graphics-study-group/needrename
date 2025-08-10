#ifndef RENDER_CONSTANTDATA_PERMODELCONSTANTS_INCLUDED
#define RENDER_CONSTANTDATA_PERMODELCONSTANTS_INCLUDED

#include <glm.hpp>

namespace vk {
    class PushConstantRange;
}

namespace Engine {
    class RenderSystem;
    
    namespace ConstantData {
        struct PerModelPushStruct {
            glm::mat4 model_matrix;
        };

        class PerModelConstantPushConstant {
        public:
            static constexpr uint32_t PUSH_RANGE_OFFSET = 0u;
            static constexpr uint32_t PUSH_RANGE_SIZE = sizeof(PerModelPushStruct);
            static vk::PushConstantRange GetPushConstantRange();
        };
    }
}

#endif // RENDER_CONSTANTDATA_PERMODELCONSTANTS_INCLUDED
