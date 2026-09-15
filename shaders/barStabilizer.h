#pragma once

#include <cstdint>

struct BarStabilizer
{
    std::uint32_t value = 0;
    std::uint32_t candidate = 0;
    std::uint32_t confirmations = 0;

    void Reset()
    {
        value = 0;
        candidate = 0;
        confirmations = 0;
    }

    std::uint32_t Update(std::uint32_t detected, std::uint32_t requiredConfirmations = 4)
    {
        // Expose newly visible image immediately. Cover more image only after
        // the same larger bar has been observed repeatedly.
        if (detected <= value)
        {
            value = detected;
            candidate = 0;
            confirmations = 0;
            return value;
        }

        if (candidate != detected)
        {
            candidate = detected;
            confirmations = 1;
        }
        else
        {
            ++confirmations;
        }

        if (confirmations >= requiredConfirmations)
        {
            value = candidate;
            candidate = 0;
            confirmations = 0;
        }

        return value;
    }
};
