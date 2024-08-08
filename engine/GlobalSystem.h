#ifndef GLOBALSYSTEM_INCLUDED
#define GLOBALSYSTEM_INCLUDED

#include <memory>

namespace Engine
{
    class SDLWindow;
    class RenderSystem;
    class WorldSystem;
    class AssetManager;

    struct GlobalSystems
    {
        std::shared_ptr<SDLWindow> window;
        std::shared_ptr<RenderSystem> renderer;
        std::shared_ptr<WorldSystem> world;
        std::shared_ptr<AssetManager> assetManager;
    };
    
    extern GlobalSystems globalSystems;
}

#endif // GLOBALSYSTEM_INCLUDED
