#ifndef RENDER_RENDERER_SKINNEDTRANSFORMTREE_INCLUDED
#define RENDER_RENDERER_SKINNEDTRANSFORMTREE_INCLUDED

#include <glm.hpp>

namespace Engine {
    class SkinnedTransformTree {
    public:
        void UpdateTransforms(const std::vector <uint32_t> & hierarchy, const std::vector <glm::mat4> & transforms);
    protected:
        static bool IsTopological(const std::vector <uint32_t> & hierarchy);
        std::vector <glm::mat4x3> m_transforms {};
    };
}

#endif // RENDER_RENDERER_SKINNEDTRANSFORMTREE_INCLUDED
