#ifndef RENDERSYSTEM_H
#define RENDERSYSTEM_H

#include <vector>
#include <memory>

namespace Engine
{
    class Material;

    class RenderSystem
    {
    public:
        // RenderSystem();
        // ~RenderSystem();

        void render();
        void RegisterMaterial(std::shared_ptr <Material>);
        
    private:
        // TODO: data: mesh, texture, light
        std::vector <std::shared_ptr<Material>> m_materials;
    };
}

#endif // RENDERSYSTEM_H