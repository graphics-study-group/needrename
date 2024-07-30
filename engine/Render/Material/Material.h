#ifndef RENDER_MATERIAL_INCLUDED
#define RENDER_MATERIAL_INCLUDED

#include <memory>
#include <glm.hpp>

namespace Engine {
    class RenderSystem;

    struct MaterialDrawContext {
        glm::mat4 model_matrix;
    };
    class Material {
    public:
        Material (std::shared_ptr<RenderSystem> system);
        virtual ~Material () = default;

        virtual void PrepareDraw(const MaterialDrawContext* context) = 0;
    protected:
        std::weak_ptr <RenderSystem> m_renderSystem;
    };
};

#endif // RENDER_MATERIAL_INCLUDED
