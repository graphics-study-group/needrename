#ifndef ENGINE_FUNCTIONAL_TIME_H
#define ENGINE_FUNCTIONAL_TIME_H

#include <cstdint>

namespace Engine {
    class TimeSystem {
    public:
        TimeSystem() = default;
        ~TimeSystem() = default;

        uint64_t GetCurrentTime() const;
        uint64_t GetCurrentFrameStartTime() const;
        uint64_t GetDeltaTime() const;
        float GetDeltaTimeInSeconds() const;
        uint64_t GetFrameCount() const;

        void NextFrame();

    protected:
        uint64_t m_frame_start_time = 0;
        uint64_t m_delta_time = 0;
        uint64_t m_frame_count = 0;
    };
} // namespace Engine

#endif // ENGINE_FUNCTIONAL_TIME_H
