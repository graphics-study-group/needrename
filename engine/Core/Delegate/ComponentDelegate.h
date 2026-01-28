#ifndef CORE_DELEGATE_COMPONENTDELEGATE_H
#define CORE_DELEGATE_COMPONENTDELEGATE_H

#include "DelegateBase.h"
#include <Framework/world/WorldSystem.h>
#include <functional>
#include <memory>

namespace Engine {
    template <typename... Args>
    class ComponentDelegate : public DelegateBase<Args...> {
    public:
        using FunctionType = std::function<void(Args...)>;

        ComponentDelegate(const ComponentDelegate &) = default;
        template <typename T>
        ComponentDelegate(WorldSystem &world, ComponentHandle comp, void (T::*method)(Args...)) : m_world(world), m_comp(comp) {
            auto ptr = m_world.GetComponent<T>(m_comp);
            m_function = [ptr, method](Args... args) { (ptr->*method)(args...); };
        }
        virtual ~ComponentDelegate() = default;

        virtual void Invoke(Args... args) const override {
            auto ptr = m_world.GetComponent(m_comp);
            if (ptr) {
                m_function(args...);
            }
        }

        bool IsValid() const {
            return !m_world.GetComponent(m_comp);
        }

    protected:
        WorldSystem &m_world;
        ComponentHandle m_comp{};
        FunctionType m_function{};
    };
} // namespace Engine

#endif // CORE_DELEGATE_COMPONENTDELEGATE_H
