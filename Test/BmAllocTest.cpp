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
    cs
} algorithm;

CRITICAL_SECTION cs;
SRWLOCK srw = SRWLOCK_INIT;
Windows::RwSpinLock lock;

int main (int argc, char ** argv) {
    InitializeCriticalSection (&cs);

    // command-line "BmAllocTest 0" = SRLOCK, "BmAllocTest 1" = RwSpinLock

    if (argc > 1) {
        if (std::strcmp (argv [1], "spinlock") == 0) algorithm = algorithm::spinlock;
        if (std::strcmp (argv [1], "srw") == 0) algorithm = algorithm::srw;
        if (std::strcmp (argv [1], "cs") == 0) algorithm = algorithm::cs;
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
    auto na = 0uLL;
    while (WaitForSingleObject (quit, 0) != WAIT_OBJECT_0) {

        // space for allocated indices

        constexpr auto max_a = (8 * sizeof (std::intptr_t) * sizeof data / sizeof data [0]) / (sizeof threads / sizeof threads [0]);
        std::size_t a [max_a] = {};

        // random number of allocations each loop

        auto n = 1u + std::rand () % max_a;
        auto i = 0u;
        for (; i != n; ++i) {
            switch (algorithm) {
                case algorithm::spinlock:
                    if (auto guard = lock.exclusively ()) {
                        if (allocator.acquire (&a [i])) {
                            ++na;
                        } else {
                            std::printf ("%u: ERROR at %u/%u after %llu\n", GetCurrentThreadId (), (unsigned) i, (unsigned) n, na);
                        }
                    }
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
            }
        }

        // and release

        while (i--) {
            switch (algorithm) {
                case algorithm::spinlock:
                    if (auto guard = lock.exclusively ()) {
                        allocator.release (a [i]);
                    }
                    break;
                case algorithm::srw:
                    AcquireSRWLockExclusive (&srw);
                    if (auto guard = lock.exclusively ()) {
                        allocator.release (a [i]);
                    }
                    ReleaseSRWLockExclusive (&srw);
                    break;
                case algorithm::cs:
                    EnterCriticalSection (&cs);
                    if (auto guard = lock.exclusively ()) {
                        allocator.release (a [i]);
                    }
                    LeaveCriticalSection (&cs);
                    break;
            }
        }

        Sleep (0);
    }
    
    // add total number of allocations done, for results

    switch (algorithm) {
        case algorithm::spinlock:
            if (auto guard = lock.exclusively ()) {
                sum += na;
            }
            break;
        case algorithm::srw:
            AcquireSRWLockExclusive (&srw);
            sum += na;
            ReleaseSRWLockExclusive (&srw);
            break;
        case algorithm::cs:
            EnterCriticalSection (&cs);
            sum += na;
            LeaveCriticalSection (&cs);
            break;
    }
    std::printf ("%u ", GetCurrentThreadId ());
    return 0;
}
