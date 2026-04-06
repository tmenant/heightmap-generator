#pragma once

#include <nfd.h>
#include <optional>
#include <stdexcept>
#include <string>

namespace Dialogs
{
    struct NFDGuard
    {
        NFDGuard()
        {
            if (NFD_Init() != NFD_OKAY)
            {
                throw std::runtime_error(NFD_GetError());
            }
        }

        ~NFDGuard()
        {
            NFD_Quit();
        }
    };

    inline std::optional<std::string> getSavePath(const nfdu8char_t *defaultName = nullptr, const nfdu8char_t *defaultPath = nullptr)
    {
        NFDGuard guard;

        nfdchar_t *outPath = nullptr;
        nfdresult_t nfdResult = NFD_SaveDialog(&outPath, nullptr, 0, defaultPath, defaultName);

        std::optional<std::string> result = std::nullopt;

        if (nfdResult == NFD_OKAY)
        {
            result = std::string(outPath);
            NFD_FreePath(outPath);
        }
        else if (nfdResult == NFD_ERROR)
        {
            throw std::runtime_error(NFD_GetError());
        }

        return result;
    }
}