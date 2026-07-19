#ifndef EDITOR_ENGINE_IMPL_INSPECTOR_HANDLEINSPECTORS_INCLUDED
#define EDITOR_ENGINE_IMPL_INSPECTOR_HANDLEINSPECTORS_INCLUDED

#include <Editor/Inspector/VarInspectorBase.h>

namespace Editor {
    class ObjectHandleInspector : public VarInspectorBase {
    public:
        void Inspect(const std::string &name, Engine::Reflection::Var var) override;
    };

    class ComponentHandleInspector : public VarInspectorBase {
    public:
        void Inspect(const std::string &name, Engine::Reflection::Var var) override;
    };
} // namespace Editor

#endif // EDITOR_ENGINE_IMPL_INSPECTOR_HANDLEINSPECTORS_INCLUDED
