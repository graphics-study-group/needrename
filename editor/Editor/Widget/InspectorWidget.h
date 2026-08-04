#ifndef EDITOR_WIDGET_INSPECTORWIDGET_INCLUDED
#define EDITOR_WIDGET_INSPECTORWIDGET_INCLUDED

#include "Widget.h"
#include <Framework/world/Handle.h>
#include <any>
#include <string>

#include <AnnoRefl/Var.h>

namespace Engine {
    class GameObject;
}

namespace Editor {
    class InspectorWidget : public Widget {
        using ObjectHandle = Engine::ObjectHandle;

    public:
        InspectorWidget(const std::string &name);
        virtual ~InspectorWidget();

        virtual void Render() override;

        virtual void SetSelectedGameObject(ObjectHandle game_object);

    protected:
        enum class InspectorMode {
            kInspectorModeGameObject,
            kInspectorModeAsset,
            kInspectorModeNone
        };

        InspectorMode m_inspector_mode{InspectorMode::kInspectorModeNone};
        std::any m_inspected_object{};
        // Available component types for adding new components
        std::vector<std::string> m_component_types{};

        void LoadAvailableComponentTypes();

    private:
    };
} // namespace Editor

#endif // EDITOR_WIDGET_INSPECTORWIDGET_INCLUDED
