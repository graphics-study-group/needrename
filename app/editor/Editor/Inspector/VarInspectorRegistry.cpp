#include "VarInspectorRegistry.h"

namespace Editor {
    void VarInspectorRegistry::Register(const std::string &type_name, std::unique_ptr<VarInspectorBase> inspector) {
        m_inspectors[type_name] = std::move(inspector);
    }

    VarInspectorBase *VarInspectorRegistry::Find(const std::string &type_name) {
        auto it = m_inspectors.find(type_name);
        if (it != m_inspectors.end()) {
            return it->second.get();
        }
        return nullptr;
    }
} // namespace Editor
