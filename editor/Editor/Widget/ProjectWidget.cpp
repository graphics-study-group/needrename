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
            // Left sidebar
            RenderSidebar();

            // Vertical splitter to resize sidebar
            ImGui::SameLine(0.0f, 0.0f);
            const float splitter_thickness = k_splitter_thickness;
            float before_right_remain = ImGui::GetContentRegionAvail().x; // width available for splitter+right pane
            ImGui::InvisibleButton("##vsplit", ImVec2(splitter_thickness, ImGui::GetContentRegionAvail().y));
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive()) {
                float delta = ImGui::GetIO().MouseDelta.x;
                // compute total content width and clamp so right pane keeps minimum width
                float total = m_sidebar_width + splitter_thickness + before_right_remain;
                float min_left = k_sidebar_min_width;
                float min_right = k_rightpane_min_width;
                m_sidebar_width = m_sidebar_width + delta;
                if (m_sidebar_width < min_left) m_sidebar_width = min_left;
                float max_left = total - splitter_thickness - min_right;
                if (m_sidebar_width > max_left) m_sidebar_width = max_left;
            }
            // optional visual
            {
                ImVec2 a = ImGui::GetItemRectMin();
                ImVec2 b = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_Separator));
            }

            // Right pane (top breadcrumb + horizontal splitter + bottom content)
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::BeginChild("RightPane", ImVec2(0, 0), false);

            // Top area: breadcrumb with adjustable height
            ImGui::BeginChild("RightTopBreadcrumb", ImVec2(0, k_breadcrumb_height), false);
            RenderBreadcrumb();
            ImGui::EndChild();

            // Bottom area: main content
            ImGui::BeginChild("RightBottomContent", ImVec2(0, 0), false);
            RenderContent();
            ImGui::EndChild();

            ImGui::EndChild(); // RightPane
        }
        ImGui::End();
    }

    void ProjectWidget::RenderBreadcrumb() {
        std::string where = m_current_path.empty() ? std::string("/") : m_current_path.generic_string();
        ImGui::TextUnformatted(where.c_str());
        ImGui::Separator();
    }

    void ProjectWidget::RenderSidebar() {
        ImGui::BeginChild("ProjectSidebar", ImVec2(m_sidebar_width, 0), true);
        ImGuiTreeNodeFlags base_flags =
            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

        auto db_ptr = m_database.lock();
        if (!db_ptr) {
            ImGui::TextUnformatted("Asset database unavailable");
            ImGui::EndChild();
            return;
        }
        auto &db = *db_ptr;

        // Project root '/'
        ImGui::PushID("root_project");
        ImGuiTreeNodeFlags flags = base_flags | ImGuiTreeNodeFlags_DefaultOpen;
        if (m_current_path.generic_string() == "/" || m_current_path.empty()) flags |= ImGuiTreeNodeFlags_Selected;
        bool open = ImGui::TreeNodeEx("/", flags);
        if (ImGui::IsItemClicked()) {
            m_current_path = "/";
        }
        if (open) {
            RenderDirTree("/", db);
            ImGui::TreePop();
        }
        ImGui::PopID();

        // Builtin root '~'
        ImGui::PushID("root_builtin");
        flags = base_flags | ImGuiTreeNodeFlags_DefaultOpen;
        if (m_current_path.generic_string() == "~") flags |= ImGuiTreeNodeFlags_Selected;
        open = ImGui::TreeNodeEx("~", flags);
        if (ImGui::IsItemClicked()) {
            m_current_path = "~";
        }
        if (open) {
            RenderDirTree("~", db);
            ImGui::TreePop();
        }
        ImGui::PopID();

        ImGui::EndChild();
    }

    void ProjectWidget::RenderDirTree(const std::filesystem::path &base_path, Engine::FileSystemDatabase &db) {
        using AssetInfo = Engine::FileSystemDatabase::AssetInfo;
        std::vector<AssetInfo> entries = db.ListDirectory(base_path);
        std::vector<std::filesystem::path> subdirs;
        subdirs.reserve(entries.size());
        for (auto &e : entries) {
            if (e.is_directory) subdirs.push_back(e.path);
        }
        std::sort(subdirs.begin(), subdirs.end(), [](const auto &a, const auto &b) {
            return a.filename().generic_string() < b.filename().generic_string();
        });

        ImGuiTreeNodeFlags base_flags =
            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

        for (const auto &p : subdirs) {
            std::string label = p.filename().generic_string();
            if (label.empty()) label = p.generic_string();
            ImGui::PushID(p.generic_string().c_str());
            ImGuiTreeNodeFlags flags = base_flags;
            if (m_current_path == p) flags |= ImGuiTreeNodeFlags_Selected;
            bool open = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked()) {
                m_current_path = p;
            }
            if (open) {
                RenderDirTree(p, db);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    void ProjectWidget::RenderContent() {
        ImGui::BeginChild("ProjectWidgetContent", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        // Ctrl + Mouse Wheel: adjust icon size when hovering the content area
        {
            ImGuiIO &io = ImGui::GetIO();
            if (io.KeyCtrl && ImGui::IsWindowHovered()) {
                float wheel = io.MouseWheel;
                if (wheel != 0.0f) {
                    // Use additive step for predictable control; clamp to reasonable bounds
                    m_tile_icon_size = std::clamp(m_tile_icon_size + wheel * k_icon_step, k_icon_min, k_icon_max);

                    // Keep text block height roughly proportional for balance (non-invasive tweak)
                    m_tile_text_height = std::clamp(m_tile_icon_size * 0.75f, k_text_min, k_text_max);
                }
            }
        }

        const float tile_w = m_tile_icon_size + k_tile_inner_padding; // include inner padding

        float content_width = ImGui::GetContentRegionAvail().x;
        if (content_width <= 0.0f) content_width = tile_w;
        int columns = (int)((content_width + k_item_spacing) / (tile_w + k_item_spacing));
        if (columns < 1) columns = 1;
        int col = 0;

        // Up tile
        if (m_current_path != "/" && m_current_path != "~") {
            std::filesystem::path parent = m_current_path.parent_path();
            if (parent != m_current_path) {
                ImGui::PushID("__up__");
                DrawTile("..", true, &parent, true, nullptr, col, columns);
                ImGui::PopID();
            }
        }

        // Directory listing
        using AssetInfo = Engine::FileSystemDatabase::AssetInfo;
        auto db_ptr = m_database.lock();
        if (db_ptr) {
            std::vector<AssetInfo> assets = db_ptr->ListDirectory(m_current_path);
            for (size_t i = 0; i < assets.size(); ++i) {
                const auto &a = assets[i];
                bool is_dir = a.is_directory;

                std::string name;
                if (is_dir) {
                    name = a.path.filename().generic_string();
                    if (name.empty()) name = a.path.generic_string();
                } else {
                    name = a.path.stem().generic_string();
                    if (name.empty()) name = a.path.filename().generic_string();
                }

                const std::filesystem::path *target = is_dir ? &a.path : nullptr;

                // Build tooltip text (path, guid, type) and pass pointer; show sensible info for directories
                std::string tooltip;
                if (is_dir) {
                    tooltip.reserve(128);
                    tooltip += "Path: ";
                    tooltip += a.path.generic_string();
                    tooltip += "\nType: <Directory>";
                } else {
                    tooltip.reserve(196);
                    tooltip += "Path: ";
                    tooltip += a.path.generic_string();
                    tooltip += "\nGUID: ";
                    tooltip += a.guid.toString();
                    tooltip += "\nType: ";
                    tooltip += (a.type_name.empty() ? std::string("<unknown>") : a.type_name);
                }

                ImGui::PushID((int)i);
                DrawTile(name, is_dir, target, false, &tooltip, col, columns);
                ImGui::PopID();
            }
        }

        ImGui::EndChild();
    }

    void ProjectWidget::WrapToLines(
        const std::string &s, float max_w, int max_lines, std::vector<std::string> &out_lines
    ) const {
        out_lines.clear();
        if (s.empty() || max_w <= 0.0f || max_lines <= 0) return;
        const char *ell = "...";
        float ell_w = ImGui::CalcTextSize(ell).x;

        size_t idx = 0;
        const size_t n = s.size();
        for (int line = 0; line < max_lines && idx < n; ++line) {
            bool last_line = (line == max_lines - 1);
            std::string cur;

            // fill characters that fit in this line
            while (idx < n) {
                char c = s[idx];
                std::string test = cur;
                test.push_back(c);
                float w = ImGui::CalcTextSize(test.c_str()).x;
                if (last_line) {
                    // reserve space for ellipsis if there will be more characters remaining
                    bool more_after = (idx + 1 < n);
                    if (w + (more_after ? ell_w : 0.0f) <= max_w) {
                        cur.swap(test);
                        ++idx;
                    } else {
                        break;
                    }
                } else {
                    if (w <= max_w) {
                        cur.swap(test);
                        ++idx;
                    } else {
                        break;
                    }
                }
            }

            if (cur.empty()) {
                // can't fit any character in this line
                if (last_line) {
                    out_lines.emplace_back(ell);
                    return;
                } else {
                    // push empty to avoid infinite loop; but typically shouldn't happen with fonts > 0
                    out_lines.emplace_back("");
                    continue;
                }
            }

            if (last_line && idx < n) {
                // not all consumed: need ellipsis
                // ensure adding ellipsis still fits
                while (!cur.empty() && ImGui::CalcTextSize((cur + ell).c_str()).x > max_w) {
                    cur.pop_back();
                }
                cur += ell;
                out_lines.emplace_back(std::move(cur));
                return;
            } else {
                out_lines.emplace_back(std::move(cur));
            }
        }
    }

    void ProjectWidget::DrawTile(
        const std::string &display_name,
        bool is_folder,
        const std::filesystem::path *target_path,
        bool is_up,
        const std::string *tooltip,
        int &col,
        int columns
    ) {
        const float tile_w = m_tile_icon_size + k_tile_inner_padding; // include inner padding
        const float tile_h = m_tile_icon_size + m_tile_text_height + k_tile_vertical_padding;

        ImGui::BeginGroup();
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("tile", ImVec2(tile_w, tile_h));
        bool hovered = ImGui::IsItemHovered();
        bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        // Tooltip on hover after a short delay
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tooltip->c_str());
            ImGui::EndTooltip();
        }

        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImU32 bg_col =
            hovered ? ImGui::GetColorU32(ImGuiCol_FrameBgHovered) : ImGui::GetColorU32(ImGuiCol_FrameBg, 0.0f);
        dl->AddRectFilled(cursor, ImVec2(cursor.x + tile_w, cursor.y + tile_h), bg_col, 6.0f);

        // Icon area
        ImVec2 icon_min(cursor.x + (tile_w - m_tile_icon_size) * 0.5f, cursor.y + k_icon_top_padding);
        ImVec2 icon_max(icon_min.x + m_tile_icon_size, icon_min.y + m_tile_icon_size);
        if (is_folder) {
            ImU32 folder_col = ImColor(255, 180, 80);
            ImVec2 tab_min(icon_min.x + m_tile_icon_size * 0.0f, icon_min.y);
            ImVec2 tab_max(icon_min.x + m_tile_icon_size * 0.3f, icon_min.y + m_tile_icon_size * 0.28f);
            ImVec2 t0(icon_min.x + m_tile_icon_size * 0.27f, icon_min.y);
            ImVec2 t1(icon_min.x + m_tile_icon_size * 0.27f, icon_min.y + m_tile_icon_size * 0.28f);
            ImVec2 t2(
                icon_min.x + m_tile_icon_size * 0.27f + m_tile_icon_size * 0.27f, icon_min.y + m_tile_icon_size * 0.27f
            );
            dl->AddRectFilled(tab_min, tab_max, folder_col, 3.0f);
            dl->AddTriangleFilled(t0, t1, t2, ImGui::GetColorU32(folder_col));
            dl->AddRectFilled(ImVec2(icon_min.x, icon_min.y + m_tile_icon_size * 0.18f), icon_max, folder_col, 5.0f);
        } else {
            ImU32 file_col = ImColor(100, 170, 255);
            ImVec2 doc_min = icon_min;
            ImVec2 doc_max = icon_max;
            dl->AddRectFilled(doc_min, doc_max, file_col, 6.0f);
            float corner = m_tile_icon_size * 0.25f;
            ImVec2 c0(doc_max.x - corner, doc_min.y);
            ImVec2 c1(doc_max.x, doc_min.y + corner);
            dl->AddTriangleFilled(c0, ImVec2(doc_max.x, doc_min.y), c1, ImGui::GetColorU32(ImGuiCol_WindowBg));
            dl->AddLine(c0, c1, ImGui::GetColorU32(ImGuiCol_Border));
        }

        // Text (wrap to configurable lines, ellipsis on last line)
        ImVec2 text_area_min(cursor.x + k_text_side_padding, icon_max.y + k_icon_text_gap);
        ImVec2 text_area_max(cursor.x + tile_w - k_text_side_padding, cursor.y + tile_h - k_text_side_padding);
        float max_w = text_area_max.x - text_area_min.x;
        std::string label = is_up ? std::string(".. ") : display_name;
        std::vector<std::string> lines;
        WrapToLines(label, max_w, k_tile_max_lines, lines);
        float line_h = ImGui::GetTextLineHeight();
        float block_h = line_h * static_cast<float>(lines.size());
        float base_y = text_area_min.y + std::max(0.0f, (text_area_max.y - text_area_min.y - block_h) * 0.5f);
        ImU32 text_col = is_up ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::GetColorU32(ImGuiCol_Text);
        dl->PushClipRect(text_area_min, text_area_max, true);
        for (size_t li = 0; li < lines.size(); ++li) {
            if (!lines[li].empty()) {
                dl->AddText(ImVec2(text_area_min.x, base_y + line_h * li), text_col, lines[li].c_str());
            }
        }
        dl->PopClipRect();

        if (double_clicked && target_path) {
            if (is_folder) {
                m_current_path = *target_path;
            }
        }
        ImGui::EndGroup();

        // Flow layout
        col++;
        if (col < columns) {
            ImGui::SameLine(0.0f, k_item_spacing);
        } else {
            col = 0;
        }
    }
} // namespace Editor
