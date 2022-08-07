#include "BmAlloc.hpp"
#include <Windows.h>
#include <cstdint>

namespace {
    constexpr auto width = 8 * sizeof (std::intptr_t); // word width in bits

    inline static bool BitScanForwardPtr (unsigned long * index, std::size_t mask) noexcept {
#ifdef _WIN64
        return _BitScanForward64 (index, mask);
#else
        return _BitScanForward (index, mask);
#endif
    }
    inline static bool BitTestAndResetForwardPtr (std::intptr_t * word, std::intptr_t index) noexcept {
#ifdef _WIN64
        return _bittestandreset64 (word, index);
#else
        return _bittestandreset ((LONG *) word, index);
#endif
    }
}

bool BmAlloc::acquire (std::size_t * result) noexcept {

    auto words = size / width;
    for (std::size_t w = 0; w != words; ++w) {

        auto index = 0uL;
        if (BitScanForwardPtr (&index, ~this->data [w])) {

            this->data [w] |= std::size_t (1) << index;
            *result = w * width + index;
            return true;
        }
    }

    auto rest = size % width;
    if (rest) {

        auto index = 0uL;
        if (BitScanForwardPtr (&index, ~this->data [words])) {
            if (index < rest) {

                this->data [words] |= std::size_t (1) << index;
                *result += words * width + index;
                return true;
            }
        }
    }
    return false;
}

bool BmAlloc::release (std::size_t index) noexcept {
    return BitTestAndResetForwardPtr (this->data + index / width, index % width);
}
