#ifndef EDITOR_INSPECTOR_DEFAULTCOMPONENTINSPECTOR_INCLUDED
#define EDITOR_INSPECTOR_DEFAULTCOMPONENTINSPECTOR_INCLUDED

namespace Engine {
    class Component;
}

namespace Editor {
    void InspectComponent(Engine::Component &component);
    void DefaultInspectComponent(Engine::Component &component);
} // namespace Editor

#endif // EDITOR_INSPECTOR_DEFAULTCOMPONENTINSPECTOR_INCLUDED
