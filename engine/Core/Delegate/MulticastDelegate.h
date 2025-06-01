#ifndef CORE_DELEGATE_MULTICASTDELEGATE_H
#define CORE_DELEGATE_MULTICASTDELEGATE_H

#include <memory>
#include <functional>
#include <vector>
#include <algorithm>
#include "DelegateBase.h"
#include "Delegate.h"

namespace Engine
{
    template <typename... Args>
    class MulticastDelegate : public DelegateBase<Args...>
    {
    public:
        using FunctionType = std::function<void(Args...)>;

        MulticastDelegate() = default;
        MulticastDelegate(const MulticastDelegate &) = default;
        virtual ~MulticastDelegate() = default;

        virtual void Invoke(Args... args) const override
        {
            for (const auto &delegate : m_delegates)
            {
                if (delegate.IsValid())
                {
                    delegate.Invoke(args...);
                }
            }
        }

        void AddDelegate(const Delegate<Args...> &delegate)
        {
            m_delegates.push_back(delegate);
        }

        template <typename T>
        void AddDelegate(std::shared_ptr<T> object, void (T::*method)(Args...))
        {
            m_delegates.emplace_back(object, method);
        }

        template <typename T>
        void AddDelegate(std::weak_ptr<T> object, void (T::*method)(Args...))
        {
            m_delegates.emplace_back(object, method);
        }

        void ClearInvalidDelegates()
        {
            m_delegates.erase(
                std::remove_if(m_delegates.begin(), m_delegates.end(),
                               [](const Delegate<Args...> &delegate)
                               { return !delegate.IsValid(); }),
                m_delegates.end());
        }

        void RemoveDelegate(const Delegate<Args...> &delegate)
        {
            m_delegates.erase(
                std::remove(m_delegates.begin(), m_delegates.end(), delegate),
                m_delegates.end());
        }

        size_t GetDelegateCount() const
        {
            return m_delegates.size();
        }

    protected:
        std::vector<Delegate<Args...>> m_delegates{};
    };
}

#endif // CORE_DELEGATE_MULTICASTDELEGATE_H
