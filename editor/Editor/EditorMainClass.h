#ifndef EDITOR_EDITORMAINCLASS_INCLUDED
#define EDITOR_EDITORMAINCLASS_INCLUDED

#include <Editor/Inspector/ComponentInspectorRegistry.h>
#include <Editor/Inspector/VarInspectorRegistry.h>
#include <memory>
#include <mutex>

namespace Editor {
    class EditorMainClass {
    public:
        [[nodiscard]]
        static std::shared_ptr<EditorMainClass> GetInstance();

        EditorMainClass() = default;
        virtual ~EditorMainClass();

        void Initialize();

        VarInspectorRegistry &GetVarInspectorRegistry() {
            return m_var_inspector_registry;
        }
        ComponentInspectorRegistry &GetComponentInspectorRegistry() {
            return m_component_inspector_registry;
        }

    private:
        static std::weak_ptr<EditorMainClass> m_instance;
        static std::once_flag m_instance_ready;
        static int test_var;

        VarInspectorRegistry m_var_inspector_registry{};
        ComponentInspectorRegistry m_component_inspector_registry{};
    };
} // namespace Editor

#endif // EDITOR_EDITORMAINCLASS_INCLUDED
