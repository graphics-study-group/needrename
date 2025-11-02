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
        std::filesystem::path m_current_path{"/"};
        std::weak_ptr<Engine::FileSystemDatabase> m_database{};

        // Layout parameters (tweakable)
        float m_sidebar_width = 240.0f;
        float m_breadcrumb_height = 28.0f;
        float m_tile_icon_size = 64.0f;
        float m_tile_text_height = 48.0f;
        float m_item_spacing = 12.0f;
        int m_tile_max_lines = 3; // max lines rendered for file names

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
