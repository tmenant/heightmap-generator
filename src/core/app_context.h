#pragma once

#include <atomic>
#include <string>

struct LoadingPayload
{
private:
    std::atomic<int> progression{ 0 };
    std::string message;
    mutable std::mutex messageMutex;

public:
    void setProgression(int value) noexcept
    {
        progression.store(value, std::memory_order_relaxed);
    }

    void setMessage(const std::string &value)
    {
        std::lock_guard<std::mutex> lock(messageMutex);
        message = value;
    }

    void update(int value, const std::string &valueMessage)
    {
        {
            std::lock_guard<std::mutex> lock(messageMutex);
            message = valueMessage;
        }
        progression.store(value, std::memory_order_relaxed);
    }

    std::string getMessage() const
    {
        std::lock_guard<std::mutex> lock(messageMutex);
        return message;
    }

    int getProgression() const noexcept
    {
        return progression.load(std::memory_order_relaxed);
    }
};

struct AppContext
{
    // loading state
    std::atomic<int> progression = 0;
    std::atomic<bool> isLoaded = false;
    std::string currentStatus;
    LoadingPayload loadingPayload;

    void load(const std::string &gamePath)
    {
        if (isLoaded) return;
        isLoaded = true;
    }
};