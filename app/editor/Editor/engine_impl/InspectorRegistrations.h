#ifndef EDITOR_ENGINE_IMPL_INSPECTORREGISTRATIONS_INCLUDED
#define EDITOR_ENGINE_IMPL_INSPECTORREGISTRATIONS_INCLUDED

namespace Editor {
    class VarInspectorRegistry;
    class ComponentInspectorRegistry;

    void RegisterAllVarInspectors(VarInspectorRegistry &registry);
    void RegisterAllComponentInspectors(ComponentInspectorRegistry &registry);
} // namespace Editor

#endif // EDITOR_ENGINE_IMPL_INSPECTORREGISTRATIONS_INCLUDED
