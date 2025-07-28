#include "RendererManager.h"

namespace Engine {
    struct RendererManager::impl {

    };
    RendererManager::RendererManager(RenderSystem & system) : m_system(system) {
    }
    RendererManager::~RendererManager() = default;
};
