#ifndef EDITOR_INSPECTOR_COMPONENTINSPECTORREGISTRY_INCLUDED
#define EDITOR_INSPECTOR_COMPONENTINSPECTORREGISTRY_INCLUDED

#include "ComponentInspectorBase.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace Editor {
    class ComponentInspectorRegistry {
    public:
        void Register(const std::string &type_name, std::unique_ptr<ComponentInspectorBase> inspector);
        ComponentInspectorBase *Find(const std::string &type_name);

    private:
        std::unordered_map<std::string, std::unique_ptr<ComponentInspectorBase>> m_inspectors{};
    };
} // namespace Editor

#endif // EDITOR_INSPECTOR_COMPONENTINSPECTORREGISTRY_INCLUDED
