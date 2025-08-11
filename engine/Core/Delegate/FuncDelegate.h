#ifndef CORE_DELEGATE_FUNCDELEGATE_H
#define CORE_DELEGATE_FUNCDELEGATE_H

#include "DelegateBase.h"
#include <functional>
#include <memory>

namespace Engine {
    template <typename... Args>
    class FuncDelegate : public DelegateBase<Args...> {
    public:
        using FunctionType = std::function<void(Args...)>;

        FuncDelegate(const FuncDelegate &) = default;
        FuncDelegate(FunctionType function) : m_function(function) {
        }

        virtual bool IsValid() const override {
            return m_function != nullptr;
        }

        virtual void Invoke(Args... args) const override {
            if (m_function) {
                m_function(args...);
            }
        }

    protected:
        FunctionType m_function{};
    };
}; // namespace Engine

#endif // CORE_DELEGATE_FUNCDELEGATE_H
