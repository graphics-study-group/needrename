#include "Time.h"
#include <SDL3/SDL.h>

namespace Engine {
    uint64_t TimeSystem::GetCurrentTime() const {
        return SDL_GetTicksNS();
    }

    uint64_t TimeSystem::GetCurrentFrameStartTime() const {
        return m_frame_start_time;
    }

    uint64_t TimeSystem::GetDeltaTime() const {
        return m_delta_time;
    }

    float TimeSystem::GetDeltaTimeInSeconds() const {
        return static_cast<float>(m_delta_time) * 1e-9f; // Convert nanoseconds to seconds
    }

    uint64_t TimeSystem::GetFrameCount() const {
        return m_frame_count;
    }

    void TimeSystem::NextFrame() {
        uint64_t current_time = SDL_GetTicksNS();
        m_delta_time = current_time - m_frame_start_time;
        m_frame_start_time = current_time;
        m_frame_count++;
    }
} // namespace Engine
