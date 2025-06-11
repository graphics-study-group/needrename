#ifndef EDITOR_WIDGET_PROJECTWIDGET_INCLUDED
#define EDITOR_WIDGET_PROJECTWIDGET_INCLUDED

#include "Widget.h"
#include <memory>

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
    };
}

#endif // EDITOR_WIDGET_PROJECTWIDGET_INCLUDED
