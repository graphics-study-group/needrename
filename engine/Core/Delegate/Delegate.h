#ifndef CORE_DELEGATE_DELEGATE_H
#define CORE_DELEGATE_DELEGATE_H

#include "DelegateBase.h"
#include <functional>
#include <memory>

namespace Engine {
    template <typename... Args>
    class Delegate : public DelegateBase<Args...> {
    public:
        using FunctionType = std::function<void(Args...)>;

        Delegate(const Delegate &) = default;
        Delegate(std::weak_ptr<void> object, FunctionType function) : m_object(object), m_function(function) {
        }
        template <typename T>
        Delegate(std::shared_ptr<T> object, void (T::*method)(Args...)) : m_object(object) {
            auto ptr = object.get();
            m_function = [ptr, method](Args... args) { (ptr->*method)(args...); };
        }
        template <typename T>
        Delegate(std::weak_ptr<T> object, void (T::*method)(Args...)) : m_object(object) {
            auto ptr = object.lock().get();
            m_function = [ptr, method](Args... args) { (ptr->*method)(args...); };
        }
        virtual ~Delegate() = default;

        virtual void Invoke(Args... args) const override {
            if (auto obj = m_object.lock()) {
                m_function(args...);
            }
        }

        bool IsValid() const {
            return !m_object.expired();
        }

    protected:
        std::weak_ptr<void> m_object{};
        FunctionType m_function{};
    };
} // namespace Engine

#endif // CORE_DELEGATE_DELEGATE_H
