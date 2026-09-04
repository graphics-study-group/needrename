#ifndef FRAMEWORK_WORLD_EVENTQUEUE_INCLUDED
#define FRAMEWORK_WORLD_EVENTQUEUE_INCLUDED

#include "Framework/framework_export.h"
#include <Framework/Component/ComponentDelegate.h>
#include <Framework/World/Scene.h>
#include <memory>
#include <queue>

namespace Engine {
    class FRAMEWORK_API EventQueue {
        using DelegatePtr = std::unique_ptr<DelegateBase<>>;

    public:
        EventQueue(Scene &world);
        virtual ~EventQueue() = default;

        // Holds a queue of move-only DelegatePtr entries; non-copyable.
        EventQueue(const EventQueue &) = delete;
        EventQueue &operator=(const EventQueue &) = delete;

        void AddEvent(DelegatePtr event);
        template <typename T>
        void AddEvent(ComponentHandle object, void (T::*method)()) {
            m_events.push(std::make_unique<ComponentDelegate<>>(m_scene, object, method));
        }
        void ProcessEvents();

        void Clear();

    protected:
        Scene &m_scene;
        std::queue<DelegatePtr> m_events{};
    };
} // namespace Engine

#endif // FRAMEWORK_WORLD_EVENTQUEUE_INCLUDED
