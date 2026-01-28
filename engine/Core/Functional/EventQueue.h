#ifndef ENGINE_FUNCTIONAL_EVENTQUEUE_H
#define ENGINE_FUNCTIONAL_EVENTQUEUE_H

#include <Core/Delegate/ComponentDelegate.h>
#include <Framework/world/WorldSystem.h>
#include <memory>
#include <queue>

namespace Engine {
    class EventQueue {
        using DelegatePtr = std::unique_ptr<DelegateBase<>>;

    public:
        EventQueue(WorldSystem &world);
        virtual ~EventQueue() = default;

        void AddEvent(DelegatePtr event);
        template <typename T>
        void AddEvent(ComponentHandle object, void (T::*method)()) {
            m_events.push(std::make_unique<ComponentDelegate<>>(m_world.GetMainSceneRef(), object, method));
        }
        void ProcessEvents();

        void Clear();

    protected:
        WorldSystem &m_world;
        std::queue<DelegatePtr> m_events{};
    };
} // namespace Engine

#endif // ENGINE_FUNCTIONAL_EVENTQUEUE_H
