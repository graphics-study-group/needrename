#ifndef CORE_DELEGATE_DELEGATEBASE_H
#define CORE_DELEGATE_DELEGATEBASE_H

#include <memory>
#include <functional>

namespace Engine
{
    template <typename... Args>
    class DelegateBase
    {
    public:
        using FunctionType = std::function<void(Args...)>;

        DelegateBase() = default;
        virtual ~DelegateBase() = default;

        virtual bool IsValid() const = 0;
        virtual void Invoke(Args... args) const = 0;
    };
}

#endif // CORE_DELEGATE_DELEGATEBASE_H
