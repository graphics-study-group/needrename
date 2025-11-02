#ifndef EDITOR_WIDGET_PROJECTWIDGET_INCLUDED
#define EDITOR_WIDGET_PROJECTWIDGET_INCLUDED

#include "Widget.h"
#include <filesystem>
#include <memory>

namespace Engine {
    class GameObject;
    class FileSystemDatabase;
}

namespace Editor {
    class ProjectWidget : public Widget {
    public:
        ProjectWidget(const std::string &name);
        virtual ~ProjectWidget();

        virtual void Render() override;

    protected:
        std::filesystem::path m_current_path{};
        std::weak_ptr<Engine::FileSystemDatabase> m_database{};
    };
} // namespace Editor

#endif // EDITOR_WIDGET_PROJECTWIDGET_INCLUDED
