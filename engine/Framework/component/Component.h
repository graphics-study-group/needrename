#ifndef FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_COMPONENT_INCLUDED

#include <memory>
#include <Framework/object/Object.h>

namespace Engine
{
    class GameObject;

    class Component: public Object
    {
    public:
        Component(std::weak_ptr<GameObject> gameObject);
        virtual ~Component() = default;

        virtual void Init();
        virtual void Tick(float dt) = 0;

    protected:
        std::weak_ptr<GameObject> m_parentGameObject;
    };
}
#endif // FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
