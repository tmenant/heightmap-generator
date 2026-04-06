#pragma once

#include <fmt/color.h>
#include <fmt/format.h>
#include <format>
#include <fstream>
#include <mutex>
#include <ostream>
#include <string>

class LoggerBase
{
protected:
    static inline std::ofstream logStream = std::ofstream("ignore/hmapgen.log");
    static inline std::mutex logMutex;

    static inline fmt::rgb colorInfo = fmt::rgb(0x88C0D0);    // rgb(0, 136, 192)
    static inline fmt::rgb colorWarning = fmt::rgb(0xF4A460); // rgb(244,164,96)
    static inline fmt::rgb colorError = fmt::rgb(0xFF6347);   // rgb(255,99,71)
};

template <typename T>
class Logger : protected LoggerBase
{
    const std::string name;

public:
    Logger() : name(getTypeName()) {}

    void log(const std::string &level, fmt::rgb color, const std::string &message)
    {
        std::lock_guard<std::mutex> lock(logMutex);
        std::string output = std::format("{} [{}] {}\n", level, name, message);

        logStream << output << std::flush;

        fmt::print(fmt::fg(color), "{}", output);
    }

    // info
    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args &&...args)
    {
        log("INF", colorInfo, std::format(fmt, std::forward<Args>(args)...));
    }

    void info(const std::string &message)
    {
        log("INF", colorInfo, message);
    }

    // warning
    template <typename... Args>
    void warning(std::format_string<Args...> fmt, Args &&...args)
    {
        log("WRN", colorWarning, std::format(fmt, std::forward<Args>(args)...));
    }

    void warning(const std::string &message)
    {
        log("WRN", colorWarning, message);
    }

    // error
    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args &&...args)
    {
        log("ERR", colorError, std::format(fmt, std::forward<Args>(args)...));
    }

    void error(const std::string &message)
    {
        log("ERR", colorError, message);
    }

private:
    static constexpr std::string_view getTypeName()
    {
        std::string_view name = __PRETTY_FUNCTION__;

        size_t start = name.find("T = ") + 4;
        size_t end = name.find_first_of(";]", start);

        if (end == std::string_view::npos)
        {
            end = name.size();
        }

        return name.substr(start, end - start);
    }
};

template <typename T>
class Loggable
{
protected:
    static inline Logger<T> logger;
};