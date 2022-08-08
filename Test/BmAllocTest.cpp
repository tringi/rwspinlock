#include <Windows.h>
#include <cstdlib>
#include <cstdio>

#include "BmAlloc.hpp"
#include "../Windows_RwSpinLock.hpp"

HANDLE quit = CreateEvent (NULL, TRUE, FALSE, NULL);
HANDLE threads [16] = {}; // change to set number of threads

DWORD WINAPI procedure (LPVOID);

std::uint64_t sum = 0u;
std::intptr_t data [32];
BmAlloc allocator (data, 8 * sizeof (std::intptr_t) * sizeof data / sizeof data [0]);

enum class algorithm {
    spinlock,
    srw,
    cs,
    mutex
} algorithm;

CRITICAL_SECTION cs;
SRWLOCK srw = SRWLOCK_INIT;
Windows::RwSpinLock lock;
HANDLE mutex = CreateMutex (NULL, FALSE, NULL);

int main (int argc, char ** argv) {
    InitializeCriticalSection (&cs);

    // command-line "BmAllocTest 0" = SRLOCK, "BmAllocTest 1" = RwSpinLock

    if (argc > 1) {
        if (std::strcmp (argv [1], "spinlock") == 0) algorithm = algorithm::spinlock;
        if (std::strcmp (argv [1], "srw") == 0) algorithm = algorithm::srw;
        if (std::strcmp (argv [1], "cs") == 0) algorithm = algorithm::cs;
        if (std::strcmp (argv [1], "mutex") == 0) algorithm = algorithm::mutex;
    }

    // create threads

    for (auto & thread : threads) {
        DWORD id;
        thread = CreateThread (NULL, 0, procedure, &allocator, CREATE_SUSPENDED, &id);
        std::printf ("%u ", id);
    }

    std::printf ("\nTesting. ");

    // start counting time and resume work

    auto t0 = GetTickCount64 ();
    for (auto thread : threads) {
        ResumeThread (thread);
    }

    std::system ("pause");

    // wait for threads to exit

    SetEvent (quit);
    Sleep (0);
    for (auto thread : threads) {
        WaitForSingleObject (thread, INFINITE);
    }

    // result

    std::printf ("\nRESULT: %llu/s\n", sum * 1000 / (GetTickCount64 () - t0));
    return 0;
}

DWORD WINAPI procedure (LPVOID) {
    std::uint64_t na = 0uLL;
    std::uint64_t spins [256] = { 0 };

    while (WaitForSingleObject (quit, 0) != WAIT_OBJECT_0) {

        // space for allocated indices

        constexpr auto max_a = (8 * sizeof (std::intptr_t) * sizeof data / sizeof data [0]) / (sizeof threads / sizeof threads [0]);
        std::size_t a [max_a] = {};

        // random number of allocations each loop

        auto n = 1u + std::rand () % max_a;
        auto i = 0u;
        for (; i != n; ++i) {
            
            std::uint32_t rounds = 0u;
            switch (algorithm) {
                case algorithm::spinlock:
                    if (auto guard = lock.exclusively (&rounds)) {
                        if (allocator.acquire (&a [i])) {
                            ++na;
                        } else {
                            std::printf ("%u: ERROR at %u/%u after %llu\n", GetCurrentThreadId (), (unsigned) i, (unsigned) n, na);
                        }
                    }
                    if (rounds > sizeof spins / sizeof spins [0] - 1) {
                        rounds = sizeof spins / sizeof spins [0] - 1;
                    }

                    ++spins [rounds];
                    break;
                case algorithm::srw:
                    AcquireSRWLockExclusive (&srw);
                    if (allocator.acquire (&a [i])) {
                        ++na;
                    } else {
                        std::printf ("%u: ERROR at %u/%u after %llu\n", GetCurrentThreadId (), (unsigned) i, (unsigned) n, na);
                    }
                    ReleaseSRWLockExclusive (&srw);
                    break;
                case algorithm::cs:
                    EnterCriticalSection (&cs);
                    if (allocator.acquire (&a [i])) {
                        ++na;
                    } else {
                        std::printf ("%u: ERROR at %u/%u after %llu\n", GetCurrentThreadId (), (unsigned) i, (unsigned) n, na);
                    }
                    LeaveCriticalSection (&cs);
                    break;
                case algorithm::mutex:
                    WaitForSingleObject (mutex, INFINITE);
                    if (allocator.acquire (&a [i])) {
                        ++na;
                    } else {
                        std::printf ("%u: ERROR at %u/%u after %llu\n", GetCurrentThreadId (), (unsigned) i, (unsigned) n, na);
                    }
                    ReleaseMutex (mutex);
                    break;
            }
        }

        // and release

        while (i--) {
            std::uint32_t rounds = 0u;
            switch (algorithm) {
                case algorithm::spinlock:
                    if (auto guard = lock.exclusively (&rounds)) {
                        allocator.release (a [i]);
                    }
                    break;
                case algorithm::srw:
                    AcquireSRWLockExclusive (&srw);
                    allocator.release (a [i]);
                    ReleaseSRWLockExclusive (&srw);
                    break;
                case algorithm::cs:
                    EnterCriticalSection (&cs);
                    allocator.release (a [i]);
                    LeaveCriticalSection (&cs);
                    break;
                case algorithm::mutex:
                    WaitForSingleObject (mutex, INFINITE);
                    allocator.release (a [i]);
                    ReleaseMutex (mutex);
                    break;
            }
        }
    }
    
    // add total number of allocations done, for results

    if (auto guard = lock.exclusively ()) {
        sum += na;

        switch (algorithm) {
            default:
                std::printf ("%6u:%10llu\n", GetCurrentThreadId (), na);
                break;
            
            case algorithm::spinlock:
                std::uint64_t allspins = 0;
                std::uint64_t highspins = 0;
                std::uint64_t totalspins = 0;
                for (auto i = 1u; i != sizeof spins / sizeof spins [0]; ++i) {
                    if (spins [i]) {
                        ++allspins;
                        totalspins += spins [i];
                        if (i >= 125) {
                            highspins += spins [i];
                        }
                    }
                }

                std::printf ("[%6u:%10llu] spins: 0 = %llu, total = %llu, high = %llu, any = %llu (total: %.3f%%, high: %.3f%%, any: %.3f%%)\n",
                             GetCurrentThreadId (), na,
                             spins [0], totalspins, highspins, allspins,
                             totalspins * 100.0 / spins [0],
                             highspins * 100.0 / spins [0],
                             allspins * 100.0 / spins [0]);
                break;
        }
        std::printf ("\n");
    }
    return 0;
}
