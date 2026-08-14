#ifndef EDITOR_ENGINE_IMPL_INSPECTOR_PHYSICSCONSTRAINTCOMPONENTINSPECTOR_INCLUDED
#define EDITOR_ENGINE_IMPL_INSPECTOR_PHYSICSCONSTRAINTCOMPONENTINSPECTOR_INCLUDED

#include <Editor/Inspector/ComponentInspectorBase.h>
#include <Framework/World/Handle.h>

namespace Editor {
    class PhysicsConstraintComponentInspector : public ComponentInspectorBase {
    public:
        void Inspect(Engine::Component &component) override;

    private:
        void InspectObjectHandle(const char *label, Engine::ObjectHandle &handle, Engine::Component &component);
    };
} // namespace Editor

#endif // EDITOR_ENGINE_IMPL_INSPECTOR_PHYSICSCONSTRAINTCOMPONENTINSPECTOR_INCLUDED
