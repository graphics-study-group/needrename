#ifndef EDITOR_WIDGET_INSPECTORWIDGET_INCLUDED
#define EDITOR_WIDGET_INSPECTORWIDGET_INCLUDED

#include "Widget.h"
#include <memory>
#include <any>
#include <string>
#include <Reflection/Var.h>

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
    
        virtual void SetSelectedGameObject(std::shared_ptr<Engine::GameObject> game_object);

    protected:
        enum class InspectorMode
        {
            kInspectorModeGameObject,
            kInspectorModeAsset,
            kInspectorModeNone
        };

        InspectorMode m_inspector_mode{InspectorMode::kInspectorModeNone};
        std::any m_inspected_object{};

        void InspectVar(const std::string &name, Engine::Reflection::Var var);
    };
}

#endif // EDITOR_WIDGET_INSPECTORWIDGET_INCLUDED
