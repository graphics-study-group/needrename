#ifndef EDITOR_WIDGET_PROJECTWIDGET_INCLUDED
#define EDITOR_WIDGET_PROJECTWIDGET_INCLUDED

#include "Widget.h"
#include <filesystem>
#include <memory>
#include <vector>

namespace Engine {
    class GameObject;
    class FileSystemDatabase;
} // namespace Engine

namespace Editor {
    class ProjectWidget : public Widget {
    public:
        ProjectWidget(const std::string &name);
        virtual ~ProjectWidget();

        virtual void Render() override;

    protected:
        static constexpr const float k_breadcrumb_height = 28.0f;
        static constexpr const float k_item_spacing = 12.0f;
        static constexpr const int k_tile_max_lines = 4; // max lines rendered for file names
        // More style/layout constants
        static constexpr const float k_splitter_thickness = 4.0f;
        static constexpr const float k_sidebar_min_width = 120.0f;
        static constexpr const float k_rightpane_min_width = 150.0f;
        static constexpr const float k_tile_inner_padding = 16.0f;
        static constexpr const float k_tile_vertical_padding = 12.0f;
        static constexpr const float k_icon_top_padding = 8.0f;
        static constexpr const float k_icon_text_gap = 4.0f;
        static constexpr const float k_text_side_padding = 6.0f;
        // Ctrl + wheel scaling parameters
        static constexpr const float k_icon_step = 8.0f;
        static constexpr const float k_icon_min = 24.0f;
        static constexpr const float k_icon_max = 160.0f;
        static constexpr const float k_text_min = 18.0f;
        static constexpr const float k_text_max = 120.0f;
        static constexpr const char *k_ellipsis = "...";

        std::filesystem::path m_current_path{"/"};
        std::weak_ptr<Engine::FileSystemDatabase> m_database{};

        // Layout parameters (tweakable)
        float m_sidebar_width{240.0f};
        float m_tile_icon_size{64.0f};
        float m_tile_text_height{48.0f};

        // Rendering helpers
        void RenderBreadcrumb();
        void RenderSidebar();
        void RenderDirTree(const std::filesystem::path &base_path, Engine::FileSystemDatabase &db);
        void RenderContent();
        void DrawTile(
            const std::string &display_name,
            bool is_folder,
            const std::filesystem::path *target_path,
            bool is_up,
            int &col,
            int columns
        );
        void WrapToLines(const std::string &s, float max_w, int max_lines, std::vector<std::string> &out_lines) const;
    };
} // namespace Editor

#endif // EDITOR_WIDGET_PROJECTWIDGET_INCLUDED
