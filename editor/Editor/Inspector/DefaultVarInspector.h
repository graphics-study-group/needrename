#ifndef EDITOR_INSPECTOR_DEFAULTVARINSPECTOR_INCLUDED
#define EDITOR_INSPECTOR_DEFAULTVARINSPECTOR_INCLUDED

#include <AnnoRefl/Var.h>
#include <string>

namespace Editor {
    void InspectVar(const std::string &name, AnnoRefl::Var var);
    void DefaultInspectVar(const std::string &name, AnnoRefl::Var var);
} // namespace Editor

#endif // EDITOR_INSPECTOR_DEFAULTVARINSPECTOR_INCLUDED
