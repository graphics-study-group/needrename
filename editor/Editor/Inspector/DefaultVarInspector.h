#ifndef EDITOR_INSPECTOR_DEFAULTVARINSPECTOR_INCLUDED
#define EDITOR_INSPECTOR_DEFAULTVARINSPECTOR_INCLUDED

#include <Reflection/Var.h>
#include <string>

namespace Editor {
    void InspectVar(const std::string &name, Engine::Reflection::Var var);
    void DefaultInspectVar(const std::string &name, Engine::Reflection::Var var);
} // namespace Editor

#endif // EDITOR_INSPECTOR_DEFAULTVARINSPECTOR_INCLUDED
