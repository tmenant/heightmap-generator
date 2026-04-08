#pragma once

#include <cstdint>
#include <fstream>
#include <ios>
#include <iostream>
#include <string>
#include <vector>

#include "types.h"

namespace File
{
    inline BytesBuffer readAllBytes(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            throw std::runtime_error("Can't open file: " + path);
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char *>(buffer.data()), size))
        {
            throw std::runtime_error("Can't open file: " + path);
        }

        return buffer;
    }

    inline void writeAllBytes(const BytesBuffer &buffer, const std::string &path)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            throw std::runtime_error("Can't open file for writing: " + path);
        }

        if (!file.write(reinterpret_cast<const char *>(buffer.data()), buffer.size()))
        {
            throw std::runtime_error("Failed to write to file: " + path);
        }
    }
}