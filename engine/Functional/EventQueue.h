#ifndef ENGINE_FUNCTIONAL_EVENTQUEUE_H
#define ENGINE_FUNCTIONAL_EVENTQUEUE_H

#include <memory>
#include <queue>
#include <Core/Delegate/Delegate.h>

namespace Engine
{
    class EventQueue
    {
        using DelegatePtr = std::unique_ptr<DelegateBase<>>;
    public:
        EventQueue() = default;
        virtual ~EventQueue() = default;

        void AddEvent(DelegatePtr event);
        template <typename T>
        void AddEvent(std::shared_ptr<T> object, void (T::*method)())
        {
            m_events.push(std::make_unique<Delegate<>>(object, method));
        }
        void ProcessEvents();

        void Clear();

    protected:
        std::queue<DelegatePtr> m_events;
    };
}

#endif // ENGINE_FUNCTIONAL_EVENTQUEUE_H
