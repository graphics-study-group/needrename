#ifndef CORE_DELEGATE_EVENT_H
#define CORE_DELEGATE_EVENT_H

#include "Delegate.h"
#include "DelegateBase.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Engine {
    typedef uint64_t DelegateHandle;

    template <typename... Args>
    class Event {
    public:
        using DelegatePtr = std::unique_ptr<DelegateBase<Args...>>;

        Event() = default;
        Event(const Event &) = default;
        virtual ~Event() = default;

        virtual void Invoke(Args... args) const {
            for (const auto &[handle, delegate] : m_delegates) {
                if (delegate->IsValid()) {
                    delegate->Invoke(args...);
                }
            }
        }

        DelegateHandle AddDelegate(DelegatePtr delegate) {
            m_delegates.push_back(std::make_pair((DelegateHandle)++m_next_handle, std::move(delegate)));
            return (DelegateHandle)m_next_handle;
        }

        template <typename T>
        DelegateHandle AddListener(std::shared_ptr<T> object, void (T::*method)(Args...)) {
            m_delegates.push_back(
                std::make_pair((DelegateHandle)++m_next_handle, std::make_unique<Delegate<Args...>>(object, method))
            );
            return (DelegateHandle)m_next_handle;
        }

        template <typename T>
        DelegateHandle AddListener(std::weak_ptr<T> object, void (T::*method)(Args...)) {
            m_delegates.push_back(
                std::make_pair((DelegateHandle)++m_next_handle, std::make_unique<Delegate<Args...>>(object, method))
            );
            return (DelegateHandle)m_next_handle;
        }

        void RemoveDelegate(DelegateHandle handle) {
            m_delegates.erase(
                std::remove_if(
                    m_delegates.begin(),
                    m_delegates.end(),
                    [handle](const std::pair<DelegateHandle, DelegatePtr> &pair_handle_delegate) {
                        return pair_handle_delegate.first == handle;
                    }
                ),
                m_delegates.end()
            );
        }

        void ClearInvalidDelegates() {
            m_delegates.erase(
                std::remove_if(
                    m_delegates.begin(),
                    m_delegates.end(),
                    [](std::pair<DelegateHandle, DelegatePtr> &pair_handle_delegate) {
                        return !pair_handle_delegate.second->IsValid();
                    }
                ),
                m_delegates.end()
            );
        }

        size_t GetDelegateCount() const {
            return m_delegates.size();
        }

    protected:
        std::vector<std::pair<DelegateHandle, DelegatePtr>> m_delegates{};
        uint64_t m_next_handle{0};
    };
} // namespace Engine

#endif // CORE_DELEGATE_EVENT_H
