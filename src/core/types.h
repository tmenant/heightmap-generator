#pragma once

#include <cstdint>
#include <string>
#include <vector>

#define DISCARD(x) (void)(x)

using byte = uint8_t;
using BytesBuffer = std::vector<uint8_t>;
using StringArray = std::vector<std::string>;

template <typename T>
concept Nullable = requires(T a) {
    { a == nullptr } -> std::convertible_to<bool>;
};

// namespace std
// {
//     template <>
//     struct [[maybe_unused]] hash<sf::Vector2i>
//     {
//         std::size_t operator()(const sf::Vector2i &v) const noexcept
//         {
//             static_assert(sizeof(std::size_t) >= 8);

//             size_t h1 = static_cast<size_t>(v.x);
//             size_t h2 = static_cast<size_t>(v.y) << 32;

//             return static_cast<size_t>(h1 | h2);
//         }
//     };
// }