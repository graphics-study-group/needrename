#ifndef EDITOR_WIDGET_PROJECTWIDGET_INCLUDED
#define EDITOR_WIDGET_PROJECTWIDGET_INCLUDED

#include "Widget.h"
#include <memory>
#include <filesystem>

namespace Engine
{
    class GameObject;
}

namespace Editor
{
    class ProjectWidget : public Widget
    {
    public:
        ProjectWidget(const std::string &name);
        virtual ~ProjectWidget();

        virtual void Render() override;
    
    protected:
        std::filesystem::path m_current_path{};
    };
}

#endif // EDITOR_WIDGET_PROJECTWIDGET_INCLUDED
