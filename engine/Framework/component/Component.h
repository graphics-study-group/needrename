#ifndef FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_COMPONENT_INCLUDED

#include <memory>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class GameObject;

    class REFL_SER_CLASS(REFL_WHITELIST) Component
    {
        REFL_SER_BODY()
    public:
        REFL_ENABLE Component() = default;
        REFL_ENABLE Component(std::weak_ptr<GameObject> gameObject);
        virtual ~Component() = default;

        virtual void Init();
        virtual void Tick(float dt);

    public:
        REFL_SER_ENABLE std::weak_ptr<GameObject> m_parentGameObject{};
    };
}
#endif // FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
