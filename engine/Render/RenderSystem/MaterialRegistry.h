#ifndef RENDER_RENDERSYSTEM_MATERIALREGISTRY_INCLUDED
#define RENDER_RENDERSYSTEM_MATERIALREGISTRY_INCLUDED

#include "Render/Pipeline/Material/MaterialLibrary.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace Engine {
    namespace RenderSystemState {
        class MaterialRegistry final : protected std::unordered_map<std::string, std::shared_ptr<MaterialLibrary>> {
            RenderSystem & m_system;

        public:
            MaterialRegistry(RenderSystem & system) : m_system(system) {};
            void Create();
            void AddMaterial(std::shared_ptr<AssetRef> ref);
            auto GetMaterial(const std::string &name) -> decltype(this->at(name));
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_MATERIALREGISTRY_INCLUDED
