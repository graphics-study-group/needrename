#ifndef RENDER_RENDERSYSTEM_MATERIALREGISTRY_INCLUDED
#define RENDER_RENDERSYSTEM_MATERIALREGISTRY_INCLUDED

#include "Render/Pipeline/Material/MaterialTemplate.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace Engine
{
    namespace RenderSystemState {
        class MaterialRegistry final : protected std::unordered_map <std::string, std::shared_ptr<MaterialTemplate>> {
            std::weak_ptr <RenderSystem> m_system {};
        public:
            MaterialRegistry() = default;
            void Create(std::weak_ptr <RenderSystem>);
            void AddMaterial(std::shared_ptr <AssetRef> ref);
            auto GetMaterial(const std::string & name) -> decltype(this->at(name));
        };
    }
} // namespace Engine


#endif // RENDER_RENDERSYSTEM_MATERIALREGISTRY_INCLUDED
