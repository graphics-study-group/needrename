#include "AssetInspector.h"
#include <Asset/AssetRef.h>
#include <Core/guid.h>
#include <imgui.h>

namespace Editor {
    void AssetRefInspector::Inspect(const std::string &name, Engine::Reflection::Var var) {
        Engine::GUID asset_guid = var.InvokeMethod("GetGUID").Get<Engine::GUID>();
        ImGui::Text("%s: [Asset]%s", name.c_str(), asset_guid.string().c_str());
    }
} // namespace Editor
