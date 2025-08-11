#ifndef CORE_DELEGATE_DELEGATEBASE_H
#define CORE_DELEGATE_DELEGATEBASE_H

#include <functional>
#include <memory>

namespace Engine {
    template <typename... Args>
    class DelegateBase {
    public:
        using FunctionType = std::function<void(Args...)>;

        DelegateBase() = default;
        virtual ~DelegateBase() = default;

        virtual bool IsValid() const = 0;
        virtual void Invoke(Args... args) const = 0;
    };
} // namespace Engine

#endif // CORE_DELEGATE_DELEGATEBASE_H
