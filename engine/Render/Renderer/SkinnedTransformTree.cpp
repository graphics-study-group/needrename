#include "SkinnedTransformTree.h"

namespace Engine {
    void SkinnedTransformTree::UpdateTransforms(const std::vector<uint32_t> &hierarchy, const std::vector<glm::mat4> &transforms)
    {
        assert(IsTopological(hierarchy));
        assert(hierarchy.size() == transforms.size());

        std::vector <glm::mat4> global_transforms {transforms.size()};
        global_transforms[0] = transforms[0];
        for (uint32_t i = 1; i < transforms.size(); i++) {
            global_transforms[i] = global_transforms[hierarchy[i]] * transforms[i];
        }
        m_transforms.clear();
        std::transform(
            global_transforms.begin(), 
            global_transforms.end(), 
            m_transforms.begin(), 
            [](const glm::mat4 & mat) {
                return glm::mat3x4(mat);
            });
    }
    bool SkinnedTransformTree::IsTopological(const std::vector<uint32_t> &hierarchy)
    {
        for (uint32_t i = 0; i < hierarchy.size(); i++) {
            if (i == 0) {
                if (hierarchy[i] != 0) return false;
            } else {
                if (hierarchy[i] >= i) return false;
            }
        }
        return true;
    }
}
