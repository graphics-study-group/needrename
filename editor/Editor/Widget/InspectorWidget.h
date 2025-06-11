#ifndef EDITOR_WIDGET_INSPECTORWIDGET_INCLUDED
#define EDITOR_WIDGET_INSPECTORWIDGET_INCLUDED

#include "Widget.h"
#include <memory>

namespace Engine
{
    class GameObject;
}

namespace Editor
{
    class InspectorWidget : public Widget
    {
    public:
        InspectorWidget(const std::string &name);
        virtual ~InspectorWidget();

        virtual void Render() override;
    };
}

#endif // EDITOR_WIDGET_INSPECTORWIDGET_INCLUDED
