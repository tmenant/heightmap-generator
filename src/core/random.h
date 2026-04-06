#pragma once

#include <cstdint>
#include <random>

class Random
{
    mutable std::mt19937 rng;
    uint32_t seed = 1337;

public:
    Random()
    {
        seed = std::random_device{}();
        rng = std::mt19937(seed);
    }

    Random(uint32_t seed) : rng(seed) {}

    int32_t nextInt32(int32_t min, int32_t max) const
    {
        std::uniform_int_distribution<int32_t> dist(min, max - 1);
        return dist(rng);
    }

    int8_t nextInt8(int8_t min, int8_t max) const
    {
        return static_cast<int8_t>(nextInt32(min, max - 1));
    }

    uint32_t getSeed()
    {
        return seed;
    }

    std::mt19937 &getRNG() const
    {
        return rng;
    }

    bool nextBool(float probability = 0.5f) const
    {
        std::bernoulli_distribution dist(probability);
        return dist(rng);
    }

    float nextFloat() const
    {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(rng);
    }
};