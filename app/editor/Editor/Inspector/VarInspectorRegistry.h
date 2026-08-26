#ifndef EDITOR_INSPECTOR_VARINSPECTORREGISTRY_INCLUDED
#define EDITOR_INSPECTOR_VARINSPECTORREGISTRY_INCLUDED

#include "VarInspectorBase.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace Editor {
    class VarInspectorRegistry {
    public:
        void Register(const std::string &type_name, std::unique_ptr<VarInspectorBase> inspector);
        VarInspectorBase *Find(const std::string &type_name);

    private:
        std::unordered_map<std::string, std::unique_ptr<VarInspectorBase>> m_inspectors{};
    };
} // namespace Editor

#endif // EDITOR_INSPECTOR_VARINSPECTORREGISTRY_INCLUDED
