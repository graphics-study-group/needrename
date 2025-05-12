#ifndef EDITOR_WIDGET_WIDGET_INCLUDED
#define EDITOR_WIDGET_WIDGET_INCLUDED

namespace Editor
{
    class Widget
    {
    public:
        Widget(const char *name);
        virtual ~Widget() = default;

        virtual void Tick(float dt);

        const char *m_name;
    };
}

#endif // EDITOR_WIDGET_WIDGET_INCLUDED
