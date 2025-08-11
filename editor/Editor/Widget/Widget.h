#ifndef EDITOR_WIDGET_WIDGET_INCLUDED
#define EDITOR_WIDGET_WIDGET_INCLUDED

#include <string>

namespace Editor {
    class Widget {
    public:
        Widget(const std::string &name);
        virtual ~Widget() = default;

        virtual void Render();

        const std::string m_name;
    };
} // namespace Editor

#endif // EDITOR_WIDGET_WIDGET_INCLUDED
