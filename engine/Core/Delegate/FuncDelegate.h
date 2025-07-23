#ifndef CORE_DELEGATE_FUNCDELEGATE_H
#define CORE_DELEGATE_FUNCDELEGATE_H

#include <memory>
#include <functional>
#include "DelegateBase.h"

namespace Engine
{
    template <typename... Args>
    class FuncDelegate : public DelegateBase<Args...>
    {
    public:
        using FunctionType = std::function<void(Args...)>;

        FuncDelegate(const FuncDelegate &) = default;
        FuncDelegate(FunctionType function) : m_function(function) {}

        virtual bool IsValid() const override
        {
            return m_function != nullptr;
        }

        virtual void Invoke(Args... args) const override
        {
            if (m_function)
            {
                m_function(args...);
            }
        }
    protected:
        FunctionType m_function{};
    };
};

#endif // CORE_DELEGATE_FUNCDELEGATE_H
