#ifndef EDITOR_WIDGET_HIERARCHYWIDGET_INCLUDED
#define EDITOR_WIDGET_HIERARCHYWIDGET_INCLUDED

#include "Widget.h"
#include <Core/Delegate/Event.h>
#include <string>
#include <Framework/world/Handle.h>

namespace Engine {
    class GameObject;
}

namespace Editor {
    class HierarchyWidget : public Widget {
        using ObjectHandle = Engine::ObjectHandle;

    public:
        HierarchyWidget(const std::string &name);
        virtual ~HierarchyWidget();

        virtual void Render() override;

    public:
        Engine::Event<ObjectHandle> m_OnGameObjectSelectedDelegate{};

    protected:
        ObjectHandle m_selected_game_object{};
        std::string m_search{};
        ObjectHandle m_renaming_game_object{};
        std::string m_rename_buffer{};
    };
} // namespace Editor

#endif // EDITOR_WIDGET_HIERARCHYWIDGET_INCLUDED
