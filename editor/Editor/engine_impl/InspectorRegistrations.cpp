#include "InspectorRegistrations.h"
#include "Inspector/AssetInspector.h"
#include "Inspector/HandleInspectors.h"
#include <Editor/Inspector/ComponentInspectorRegistry.h>
#include <Editor/Inspector/VarInspectorRegistry.h>

namespace Editor {
    void RegisterAllVarInspectors(VarInspectorRegistry &registry) {
        registry.Register("Engine::ObjectHandle", std::make_unique<ObjectHandleInspector>());
        registry.Register("Engine::ComponentHandle", std::make_unique<ComponentHandleInspector>());
        registry.Register("Engine::AssetRef", std::make_unique<AssetRefInspector>());
    }

    void RegisterAllComponentInspectors(ComponentInspectorRegistry &registry) {
        (void)registry;
    }
} // namespace Editor
