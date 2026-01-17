#ifndef EDITOR_WIDGET_PROJECTWIDGET_INCLUDED
#define EDITOR_WIDGET_PROJECTWIDGET_INCLUDED

#include "Widget.h"
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <Asset/AssetDatabase/FileSystemDatabase.h>

namespace Engine {
    class GameObject;
    class FileSystemDatabase;
} // namespace Engine

namespace Editor {
    class ProjectWidget : public Widget {
        using AssetPath = Engine::AssetPath;

    public:
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

        ProjectWidget(const std::string &name, Engine::FileSystemDatabase &database);
        virtual ~ProjectWidget();

        virtual void Render() override;

    protected:
        Engine::FileSystemDatabase &m_database;
        AssetPath m_current_path;

        // Layout parameters (tweakable)
        float m_sidebar_width{240.0f};
        float m_tile_icon_size{64.0f};
        float m_tile_text_height{48.0f};

        // Cached directory content for performance
        struct CachedEntry {
            AssetPath path;
            bool is_directory{false};
            std::string display_name{};               // original (pre-wrap) label
            std::string tooltip{};                    // prebuilt tooltip
            std::vector<std::string> wrapped_lines{}; // wrapped to cached wrap width
        };
        struct CachedDir {
            std::vector<CachedEntry> entries{};
            float wrap_width_used{-1.0f};
        };
        std::unordered_map<AssetPath, CachedDir, AssetPath::Hash> m_dir_cache{};
        std::unordered_set<AssetPath, AssetPath::Hash> m_open_dirs{};            // dirs currently open in sidebar (this frame)

        // Rendering helpers
        void RenderBreadcrumb();
        void RenderSidebar();
        void RenderDirTree(const AssetPath &base_path);
        void RenderContent();
        void DrawTile(
            const std::string &display_name,
            bool is_folder,
            const AssetPath &target_path,
            bool is_up,
            const std::string &tooltip,
            const std::vector<std::string> &prewrapped_lines,
            int &current_col,
            int total_columns
        );

        // Ensure listing exists; when wrap_width > 0, also ensure wrapped_lines match that width
        void EnsureDirCache(const AssetPath &dir, float wrap_width);
        // Remove caches for directories that are not open in sidebar and not current content dir
        void PruneDirCacheAfterSidebar();
    };
} // namespace Editor

#endif // EDITOR_WIDGET_PROJECTWIDGET_INCLUDED
