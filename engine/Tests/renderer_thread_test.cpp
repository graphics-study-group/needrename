#include <SDL3/SDL.h>
#include <iostream>
#include "Core/Functional/SDLWindow.h"
#include "Render/FullRenderSystem.h"
#include "Render/RenderThread/RenderThread.h"
#include "Render/RenderThread/RenderThreadState.h"

using namespace Engine;

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO))
        return -1;

    constexpr auto sdl_window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    auto window = std::make_shared<SDLWindow>("TEST", 400, 300, sdl_window_flags);

    auto thd = RenderThread{window};
    auto & s = *thd.BlockAndAcquireThreadState().render_system;
    auto p = s.GetDeviceInterface().GetPhysicalDevice().getProperties();
    
    std::cout << p.deviceName << "\n";

    return 0;
}
