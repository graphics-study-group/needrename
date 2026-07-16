#ifndef EDITOR_ENGINE_IMPL_INSPECTOR_ASSETINSPECTOR_INCLUDED
#define EDITOR_ENGINE_IMPL_INSPECTOR_ASSETINSPECTOR_INCLUDED

#include <Editor/Inspector/VarInspectorBase.h>

namespace Editor {
    class AssetRefInspector : public VarInspectorBase {
    public:
        void Inspect(const std::string &name, Engine::Reflection::Var var) override;
    };
} // namespace Editor

#endif // EDITOR_ENGINE_IMPL_INSPECTOR_ASSETINSPECTOR_INCLUDED
