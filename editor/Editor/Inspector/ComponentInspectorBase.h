#ifndef EDITOR_INSPECTOR_COMPONENTINSPECTORBASE_INCLUDED
#define EDITOR_INSPECTOR_COMPONENTINSPECTORBASE_INCLUDED

namespace Engine {
    class Component;
}

namespace Editor {
    struct ComponentInspectorBase {
        virtual ~ComponentInspectorBase() = default;
        virtual void Inspect(Engine::Component &component) = 0;
    };
} // namespace Editor

#endif // EDITOR_INSPECTOR_COMPONENTINSPECTORBASE_INCLUDED
