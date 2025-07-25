#include "ProjectWidget.h"
#include <Asset/AssetManager/AssetManager.h>
#include <MainClass.h>
#include <imgui.h>

namespace Editor {
    ProjectWidget::ProjectWidget(const std::string &name) : Widget(name) {
    }

    ProjectWidget::~ProjectWidget() {
    }

    void ProjectWidget::Render() {
        if (ImGui::Begin(m_name.c_str())) {
            auto asset_manager = Engine::MainClass::GetInstance()->GetAssetManager();
            std::filesystem::path abs_path = asset_manager->GetAssetsDirectory() / this->m_current_path;
            if (ImGui::Button("..")) {
                if (!this->m_current_path.empty()) {
                    this->m_current_path = this->m_current_path.parent_path();
                }
            }
            for (const auto &entry : std::filesystem::directory_iterator(abs_path)) {
                if (entry.is_directory()) {
                    if (ImGui::Button(entry.path().filename().string().c_str())) {
                        this->m_current_path /= entry.path().filename();
                    }
                } else if (entry.is_regular_file() && entry.path().extension() == ".asset")
                    ImGui::Button(entry.path().filename().string().c_str());
            }
        }
        ImGui::End();
    }
} // namespace Editor
