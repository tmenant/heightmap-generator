#pragma once

#include <chrono>
#include <cmath>
#include <format>
#include <ratio>

struct Timer
{
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    TimePoint m_startTime;
    Timer() : m_startTime(Clock::now()) {}

    inline static Timer start()
    {
        return Timer();
    }

    inline void restart()
    {
        m_startTime = Clock::now();
    }

    inline float elapsedMiliseconds(bool reset = false)
    {
        TimePoint endTime = Clock::now();
        std::chrono::duration<float, std::milli> elapsed = endTime - m_startTime;

        if (reset)
        {
            m_startTime = endTime;
        }

        return elapsed.count();
    }

    inline float elapsedSeconds(bool reset = false)
    {
        return elapsedMiliseconds(reset) / 1000;
    }

    inline std::string chrono(bool reset = false)
    {
        int totalSeconds = static_cast<int>(std::floor(elapsedMiliseconds(reset) / 1000.0));

        int seconds = totalSeconds % 60;
        int minutes = (totalSeconds / 60) % 60;
        int hours = (totalSeconds / 3600);

        return std::format("{:02}:{:02}:{:02}", hours, minutes, seconds);
    }
};
