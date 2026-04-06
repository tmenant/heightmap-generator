#pragma once

#include "types.h"
#include <fmt/format.h>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>

namespace Exceptions
{
    class FileEndNotReached : public std::runtime_error
    {
    public:
        explicit FileEndNotReached(size_t offset, size_t size) noexcept :
                std::runtime_error(fmt::format("File end not reached: {} / {}", offset, size)) {}
    };

    class NotImplemented : public std::runtime_error
    {
    public:
        explicit NotImplemented() noexcept :
                std::runtime_error("Not Implemented.") {}
    };

    class NullPtr : public std::runtime_error
    {
    public:
        explicit NullPtr(std::string message = "") noexcept :
                std::runtime_error(message) {}
    };

    class AssertionException : public std::runtime_error
    {
    public:
        explicit AssertionException(std::string message = "") noexcept :
                std::runtime_error(message) {}
    };

    class IndexOutOfBounds : public std::runtime_error
    {
    public:
        explicit IndexOutOfBounds(const size_t &index, const size_t &maxSize) noexcept :
                std::runtime_error(fmt::format("Index out of bounds: {} / {}", index, maxSize)) {}
    };
}

namespace Assert
{
    inline void isTrue(bool condition, const std::string &message = "")
    {
        if (!condition) [[unlikely]]
        {
            throw Exceptions::AssertionException(message);
        }
    }

    inline void isFalse(bool condition, const std::string &message = "")
    {
        if (condition) [[unlikely]]
        {
            throw Exceptions::AssertionException(message);
        }
    }

    inline void notNull(const Nullable auto &ptr, const std::string &message = "")
    {
        if (ptr == nullptr) [[unlikely]]
        {
            throw Exceptions::NullPtr(message);
        }
    }

    inline void indexInBounds(const size_t &index, const size_t &maxSize)
    {
        if (index >= maxSize) [[unlikely]]
        {
            throw Exceptions::IndexOutOfBounds(index, maxSize);
        }
    }

    inline void fileEndReached(const size_t &offset, const size_t &size)
    {
        if (offset != size) [[unlikely]]
        {
            throw Exceptions::FileEndNotReached(offset, size);
        }
    }

    inline void equals(int32_t value1, int32_t value2, const std::string &message = "")
    {
        if (value1 != value2) [[unlikely]]
        {
            throw Exceptions::AssertionException(std::format("'{}' found, expected: '{}'", value1, value2));
        }
    }

    inline void equals(const std::string &value1, const std::string &value2, const std::string &message = "")
    {
        if (value1 != value2) [[unlikely]]
        {
            throw Exceptions::AssertionException(std::format("'{}' found, expected: '{}'", value1, value2));
        }
    }

    inline void notEquals(size_t value1, size_t value2, const std::string &message = "")
    {
        if (value1 == value2) [[unlikely]]
        {
            throw Exceptions::AssertionException(std::format("'{}' should be different than: '{}'", value1, value2));
        }
    }

    inline void lt(int32_t value1, int32_t value2)
    {
        Assert::isTrue(value1 < value2, std::format("value: {}, expected: < {}", value1, value2));
    }

    inline void le(int32_t value1, int32_t value2)
    {
        Assert::isTrue(value1 <= value2, std::format("value: {}, expected: <= {}", value1, value2));
    }

    inline void gt(int32_t value1, int32_t value2)
    {
        Assert::isTrue(value1 > value2, std::format("value: {}, expected: > {}", value1, value2));
    }

    inline void ge(int32_t value1, int32_t value2)
    {
        Assert::isTrue(value1 >= value2, std::format("value: {}, expected: >= {}", value1, value2));
    }

    inline void lt(float value1, float value2)
    {
        Assert::isTrue(value1 < value2, std::format("value: {}, expected: < {}", value1, value2));
    }

    inline void le(float value1, float value2)
    {
        Assert::isTrue(value1 <= value2, std::format("value: {}, expected: <= {}", value1, value2));
    }

    inline void gt(float value1, float value2)
    {
        Assert::isTrue(value1 > value2, std::format("value: {}, expected: > {}", value1, value2));
    }

    inline void ge(float value1, float value2)
    {
        Assert::isTrue(value1 >= value2, std::format("value: {}, expected: >= {}", value1, value2));
    }
}