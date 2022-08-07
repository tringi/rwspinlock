#ifndef BMALLOC_HPP
#define BMALLOC_HPP

#include <cstddef>
#include <cstdint>

// BmAlloc
//  - indirect bitmap allocator
//
class BmAlloc {
    std::intptr_t * data;
public:

    // size
    //  - size, in bits, of bitmap the 'data' points to
    //  - since user provided the buffer, user can change the size as appropriate
    //
    std::size_t size;

    // Requirement
    //  - size of array of std::intptr_t's to contain at least 'n' bits 
    //
    static constexpr std::size_t Requirement (std::size_t n) noexcept {
        return (n - 1) / (8 * sizeof (std::size_t)) + 1;
    }

public:
    explicit inline BmAlloc (std::intptr_t * data, std::size_t size) noexcept
        : data (data)
        , size (size) {};

    // acquire
    //  - searches 'data' of 'size' bits for first unset bit, sets it and provides back its index
    //  - if all bits are set, false is returned
    //  - REQUIRES lock when used from multiple threads
    //
    bool acquire (std::size_t * index) noexcept;

    // release
    //  - resets the bit and returns previous value
    //  - REQUIRES lock when used from multiple threads
    //
    bool release (std::size_t index) noexcept;
};

#endif
