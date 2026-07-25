#ifndef FRAMEWORK_COMPONENT_COMPONENTDELEGATE_H
#define FRAMEWORK_COMPONENT_COMPONENTDELEGATE_H

#include <Core/Delegate/DelegateBase.h>
#include <Framework/world/Scene.h>
#include <functional>
#include <memory>

namespace Engine {
    template <typename... Args>
    class ComponentDelegate : public DelegateBase<Args...> {
    public:
        using FunctionType = std::function<void(Args...)>;

        ComponentDelegate(const ComponentDelegate &) = default;
        template <typename T>
        ComponentDelegate(Scene &scene, ComponentHandle comp, void (T::*method)(Args...)) :
            m_scene(scene), m_comp(comp) {
            auto ptr = m_scene.GetComponent<T>(m_comp);
            m_function = [ptr, method](Args... args) { (ptr->*method)(args...); };
        }
        virtual ~ComponentDelegate() = default;

        virtual void Invoke(Args... args) const override {
            auto ptr = m_scene.GetComponent(m_comp);
            if (ptr) {
                m_function(args...);
            }
        }

        virtual bool IsValid() const override {
            return m_scene.GetComponent(m_comp) != nullptr;
        }

    protected:
        Scene &m_scene;
        ComponentHandle m_comp{};
        FunctionType m_function{};
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_COMPONENTDELEGATE_H
