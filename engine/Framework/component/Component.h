#ifndef FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_COMPONENT_INCLUDED

#include <memory>

namespace Engine
{
    class GameObject;

    class Component
    {
    public:
        Component(std::weak_ptr<GameObject> gameObject);
        virtual ~Component();

        virtual void Load();
        virtual void Unload();
        virtual void Tick(float dt) = 0;

    protected:
        std::weak_ptr<GameObject> m_parentGameObject;
    };
}
#endif // FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
