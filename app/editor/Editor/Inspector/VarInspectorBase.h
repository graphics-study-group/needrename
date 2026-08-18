#ifndef EDITOR_INSPECTOR_VARINSPECTORBASE_INCLUDED
#define EDITOR_INSPECTOR_VARINSPECTORBASE_INCLUDED

#include <AnnoRefl/Var.h>
#include <string>

namespace Editor {
    struct VarInspectorBase {
        virtual ~VarInspectorBase() = default;
        virtual void Inspect(const std::string &name, AnnoRefl::Var var) = 0;
    };
} // namespace Editor

#endif // EDITOR_INSPECTOR_VARINSPECTORBASE_INCLUDED
