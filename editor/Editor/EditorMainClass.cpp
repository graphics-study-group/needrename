#include "EditorMainClass.h"
#include <Editor/engine_impl/InspectorRegistrations.h>

namespace Editor {
    std::weak_ptr<EditorMainClass> EditorMainClass::m_instance{};
    std::once_flag EditorMainClass::m_instance_ready{};

    std::shared_ptr<EditorMainClass> EditorMainClass::GetInstance() {
        if (!m_instance.expired()) {
            return m_instance.lock();
        }

        std::shared_ptr<EditorMainClass> sptr{nullptr};
        // XXX: editor.dll crash when using this
        // std::call_once(m_instance_ready, [&] {
        sptr = std::make_shared<EditorMainClass>();
        m_instance = sptr;
        // });
        return sptr;
    }

    EditorMainClass::~EditorMainClass() = default;

    void EditorMainClass::Initialize() {
        RegisterAllVarInspectors(m_var_inspector_registry);
        RegisterAllComponentInspectors(m_component_inspector_registry);
    }
} // namespace Editor
