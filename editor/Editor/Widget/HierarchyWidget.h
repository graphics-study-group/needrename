#ifndef EDITOR_WIDGET_HIERARCHYWIDGET_INCLUDED
#define EDITOR_WIDGET_HIERARCHYWIDGET_INCLUDED

#include "Widget.h"
#include <Core/Delegate/Event.h>
#include <memory>
#include <string>

namespace Engine {
    class GameObject;
}

namespace Editor {
    class HierarchyWidget : public Widget {
    public:
        HierarchyWidget(const std::string &name);
        virtual ~HierarchyWidget();

        virtual void Render() override;

    public:
        Engine::Event<std::weak_ptr<Engine::GameObject>> m_OnGameObjectSelectedDelegate{};

    protected:
        std::weak_ptr<Engine::GameObject> m_selected_game_object{};
        std::string m_search{};
    };
} // namespace Editor

#endif // EDITOR_WIDGET_HIERARCHYWIDGET_INCLUDED
