#include "ProjectWidget.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <MainClass.h>
#include <algorithm>
#include <imgui.h>
#include <string>
#include <vector>

namespace Editor {
    ProjectWidget::ProjectWidget(const std::string &name) : Widget(name) {
        m_database =
            std::dynamic_pointer_cast<Engine::FileSystemDatabase>(Engine::MainClass::GetInstance()->GetAssetDatabase());
        assert(!m_database.expired());
    }

    ProjectWidget::~ProjectWidget() {
    }

    void ProjectWidget::Render() {
        if (ImGui::Begin(m_name.c_str())) {
            using AssetInfo = Engine::FileSystemDatabase::AssetInfo;
            std::vector<AssetInfo> assets = m_database.lock()->ListDirectory(m_current_path);

            // Layout parameters
            const float tile_icon_size = 64.0f;
            const float tile_text_height = 32.0f;        // reserved height for 1-2 lines of text
            const float tile_w = tile_icon_size + 16.0f; // include inner padding
            const float tile_h = tile_icon_size + tile_text_height + 12.0f;
            const float item_spacing = 12.0f;

            // Top path bar (breadcrumb-like simple text)
            {
                std::string where = m_current_path.empty() ? std::string("/") : m_current_path.generic_string();
                ImGui::TextUnformatted(where.c_str());
                ImGui::Separator();
            }

            // Scrollable content region for tiling assets
            ImGui::BeginChild("ProjectWidgetContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

            // Compute how many columns we can fit in current width
            float content_width = ImGui::GetContentRegionAvail().x;
            if (content_width <= 0.0f) content_width = tile_w; // avoid div-by-zero
            int columns = (int)((content_width + item_spacing) / (tile_w + item_spacing));
            if (columns < 1) columns = 1;
            int col = 0;

            auto draw_tile = [&](const std::string &display_name,
                                 bool is_folder,
                                 const std::filesystem::path *target_path,
                                 bool is_up = false) {
                // Begin group to keep icon + text together
                ImGui::BeginGroup();
                // Reserve the full tile area and draw custom content inside
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("tile", ImVec2(tile_w, tile_h));
                bool hovered = ImGui::IsItemHovered();
                bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImU32 bg_col =
                    hovered ? ImGui::GetColorU32(ImGuiCol_FrameBgHovered) : ImGui::GetColorU32(ImGuiCol_FrameBg, 0.0f);
                // Draw tile background
                dl->AddRectFilled(cursor, ImVec2(cursor.x + tile_w, cursor.y + tile_h), bg_col, 6.0f);

                // Icon area
                ImVec2 icon_min(cursor.x + (tile_w - tile_icon_size) * 0.5f, cursor.y + 8.0f);
                ImVec2 icon_max(icon_min.x + tile_icon_size, icon_min.y + tile_icon_size);
                if (is_folder) {
                    // Folder: draw a filled rounded rect in orange-like color
                    ImU32 folder_col = ImColor(255, 180, 80);
                    // Tab on top
                    ImVec2 tab_min(icon_min.x + tile_icon_size * 0.0f, icon_min.y);
                    ImVec2 tab_max(icon_min.x + tile_icon_size * 0.3f, icon_min.y + tile_icon_size * 0.28f);
                    ImVec2 t0(icon_min.x + tile_icon_size * 0.27f, icon_min.y);
                    ImVec2 t1(icon_min.x + tile_icon_size * 0.27f, icon_min.y + tile_icon_size * 0.28f);
                    ImVec2 t2(icon_min.x + tile_icon_size * 0.27f + tile_icon_size * 0.27f, icon_min.y + tile_icon_size * 0.27f);
                    dl->AddRectFilled(tab_min, tab_max, folder_col, 3.0f);
                    dl->AddTriangleFilled(t0, t1, t2, ImGui::GetColorU32(folder_col));
                    // Body
                    dl->AddRectFilled(
                        ImVec2(icon_min.x, icon_min.y + tile_icon_size * 0.18f), icon_max, folder_col, 5.0f
                    );
                } else {
                    // File: draw a blue-ish document shape (rectangle with a folded corner)
                    ImU32 file_col = ImColor(100, 170, 255);
                    ImVec2 doc_min = icon_min;
                    ImVec2 doc_max = icon_max;
                    // main body
                    dl->AddRectFilled(doc_min, doc_max, file_col, 6.0f);
                    // folded corner
                    float corner = tile_icon_size * 0.25f;
                    ImVec2 c0(doc_max.x - corner, doc_min.y);
                    ImVec2 c1(doc_max.x, doc_min.y + corner);
                    dl->AddTriangleFilled(c0, ImVec2(doc_max.x, doc_min.y), c1, ImGui::GetColorU32(ImGuiCol_WindowBg));
                    dl->AddLine(c0, c1, ImGui::GetColorU32(ImGuiCol_Border));
                }

                // Name text (drawn via draw list to avoid cursor reposition asserts)
                ImVec2 text_area_min(cursor.x + 6.0f, icon_max.y + 4.0f);
                ImVec2 text_area_max(cursor.x + tile_w - 6.0f, cursor.y + tile_h - 6.0f);
                float max_w = text_area_max.x - text_area_min.x;
                std::string label = is_up ? std::string(".. ") : display_name;
                // Fit to available width with simple ellipsis
                auto fit_with_ellipsis = [&](const std::string &s) -> std::string {
                    ImVec2 sz = ImGui::CalcTextSize(s.c_str());
                    if (sz.x <= max_w) return s;
                    const char *ell = "...";
                    float ell_w = ImGui::CalcTextSize(ell).x;
                    std::string out;
                    out.reserve(s.size());
                    for (size_t i = 0; i < s.size(); ++i) {
                        out.push_back(s[i]);
                        ImVec2 cur = ImGui::CalcTextSize(out.c_str());
                        if (cur.x + ell_w > max_w) {
                            if (!out.empty()) out.pop_back();
                            out += ell;
                            break;
                        }
                    }
                    if (out.empty()) return std::string(ell);
                    return out;
                };
                std::string to_draw = fit_with_ellipsis(label);

                ImVec2 text_size = ImGui::CalcTextSize(to_draw.c_str());
                float text_x = text_area_min.x; // left-align inside area
                float text_y =
                    text_area_min.y + std::max(0.0f, (text_area_max.y - text_area_min.y - text_size.y) * 0.5f);
                ImU32 text_col = is_up ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::GetColorU32(ImGuiCol_Text);
                dl->PushClipRect(text_area_min, text_area_max, true);
                dl->AddText(ImVec2(text_x, text_y), text_col, to_draw.c_str());
                dl->PopClipRect();

                // Handle double-click action
                if (double_clicked && target_path) {
                    if (is_folder) {
                        m_current_path = *target_path;
                    }
                    // Files: no open action for now
                }
                ImGui::EndGroup();

                // Layout next tile on same line when possible
                col++;
                if (col < columns) {
                    ImGui::SameLine(0.0f, item_spacing);
                } else {
                    col = 0;
                }
            };

            // Optional "up" tile if we are not at root
            auto is_root = [&]() -> bool {
                if (m_current_path.empty()) return true; // project root shorthand
                auto s = m_current_path.generic_string();
                return (s == "/" || s == "~");
            };

            col = 0; // reset column counter for the row flow
            if (!is_root()) {
                std::filesystem::path parent = m_current_path.parent_path();
                // Avoid infinite loop like parent("/") == "/" by normalizing
                if (parent != m_current_path) {
                    ImGui::PushID("__up__");
                    draw_tile("..", true, &parent, true);
                    ImGui::PopID();
                }
            }

            // Draw assets as tiles
            for (size_t i = 0; i < assets.size(); ++i) {
                const auto &a = assets[i];
                bool is_dir = a.is_directory;

                // Derive display name
                std::string name;
                if (is_dir) {
                    name = a.path.filename().generic_string();
                    if (name.empty()) name = a.path.generic_string();
                } else {
                    // Prefer stem without extension for files
                    name = a.path.stem().generic_string();
                    if (name.empty()) name = a.path.filename().generic_string();
                }

                // Target path to navigate into (folders only)
                const std::filesystem::path *target = is_dir ? &a.path : nullptr;

                ImGui::PushID((int)i);
                draw_tile(name, is_dir, target);
                ImGui::PopID();
            }

            ImGui::EndChild();
        }
        ImGui::End();
    }
} // namespace Editor
