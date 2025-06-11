#ifndef EDITOR_WIDGET_HIERARCHYWIDGET_INCLUDED
#define EDITOR_WIDGET_HIERARCHYWIDGET_INCLUDED

#include "Widget.h"
#include <memory>
#include <Core/Delegate/MulticastDelegate.h>

namespace Engine
{
    class GameObject;
}

namespace Editor
{
    class HierarchyWidget : public Widget
    {
    public:
        HierarchyWidget(const std::string &name);
        virtual ~HierarchyWidget();

        virtual void Render() override;
    
    public:
        Engine::MulticastDelegate<std::shared_ptr<Engine::GameObject>> m_OnGameObjectSelectedDelegate{};

    protected:
        std::shared_ptr<Engine::GameObject> m_selected_game_object{nullptr};
    };
}

#endif // EDITOR_WIDGET_HIERARCHYWIDGET_INCLUDED
