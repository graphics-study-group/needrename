#include "EventQueue.h"

namespace Engine {
    void EventQueue::AddEvent(DelegatePtr event) {
        m_events.push(std::move(event));
    }

    void EventQueue::ProcessEvents() {
        // TODO: try parallel processing
        while (!m_events.empty()) {
            auto event = std::move(m_events.front());
            m_events.pop();
            if (event && event->IsValid()) {
                event->Invoke();
            }
        }
    }

    void EventQueue::Clear() {
        while (!m_events.empty()) {
            m_events.pop();
        }
    }
} // namespace Engine
