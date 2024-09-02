#include "Synchronization.h"
#include "Render/RenderSystem.h"
#include <SDL3/SDL.h>

namespace Engine
{
    Synchronization::Synchronization(const RenderSystem & system) : m_system(system) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating synchronization primitives.");
    }
} // namespace Engine


