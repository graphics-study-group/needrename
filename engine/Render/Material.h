#ifndef RENDER_MATERIAL_INCLUDED
#define RENDER_MATERIAL_INCLUDED

#include <memory>

namespace Engine {
    class RenderSystem;
    class Material {
    public:
        Material (std::shared_ptr<RenderSystem> system);
        virtual ~Material () = default;

        virtual void PrepareDraw(/*Context, Transform, etc.*/) = 0;
    protected:
        std::weak_ptr <RenderSystem> m_renderSystem;
    };
};

#endif // RENDER_MATERIAL_INCLUDED
