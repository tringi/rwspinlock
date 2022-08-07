#ifndef WINDOWS_RWSPINLOCK_HPP
#define WINDOWS_RWSPINLOCK_HPP

#include <Windows.h>
#include <algorithm>
#include <cstdint>

namespace Windows {
    class RwSpinLock;
    class RwSpinLockScopeShared;
    class RwSpinLockScopeExclusive;

    // RwSpinLock
    //  - slim, cross-process, reader-writter spin lock implementation
    //  - writers don't have priority
    //
    class RwSpinLock {

        // state
        //  - 0 - unowned
        //  - -1 - owned exclusively (for write/modify operations)
        //  - +1 and above, number of shared readers
        //
        long state = 0;

    private:
        static constexpr long ExclusivelyOwned = -1;
        struct Parameters { // NOTE: might need additional tuning
            struct Exclusive {
                static constexpr auto Yields = 125u;
                static constexpr auto Sleep0s = 2u;
            };
            struct Shared {
                static constexpr auto Yields = 120u;
                static constexpr auto Sleep0s = 7u;
            };
            struct Upgrade {
                static constexpr auto Yields = 27u;
                static constexpr auto Sleep0s = 100u;
            };
        };

    public:

        // C++ style "smart" if-scope operations

        [[nodiscard]] inline RwSpinLockScopeExclusive exclusively (std::uint32_t * rounds = nullptr) noexcept;
        [[nodiscard]] inline RwSpinLockScopeExclusive exclusively (std::uint64_t timeout, std::uint32_t * rounds = nullptr) noexcept;

        [[nodiscard]] inline RwSpinLockScopeShared share (std::uint32_t * rounds = nullptr) noexcept;
        [[nodiscard]] inline RwSpinLockScopeShared share (std::uint64_t timeout, std::uint32_t * rounds = nullptr) noexcept;

    public:

        // simple locking pattern

        inline void acquire () noexcept { this->AcquireExclusive (); }
        inline void release () noexcept { this->ReleaseExclusive (); }

        [[nodiscard]] inline bool acquire (std::uint64_t timeout) noexcept { return this->AcquireExclusive (timeout); }
        inline void release (std::uint64_t timeout) noexcept { return this->ReleaseExclusive (); }

    public:

        // full API

        // TryAcquireExclusive
        //  - attempts to acquire exclusive/write lock, returns result
        //
        [[nodiscard]] inline bool TryAcquireExclusive () noexcept {
            return this->state == 0
                && InterlockedCompareExchange (&this->state, ExclusivelyOwned, 0) == 0;
        }

        // TryAcquireShared
        //  - attempts to acquire shared/read lock, returns result
        //
        [[nodiscard]] inline bool TryAcquireShared () noexcept {
            auto s = this->state;
            return s != ExclusivelyOwned
                && InterlockedCompareExchange (&this->state, s + 1, s) == s;
        }

        // ReleaseExclusive
        //  - releases all and any locks
        //
        inline void ReleaseExclusive () noexcept {
            InterlockedExchange (&this->state, 0);
        }

        // ReleaseShared
        //  - releases one shared/read lock
        //
        inline void ReleaseShared () noexcept {
            InterlockedDecrement (&this->state);
        }

        // AcquireExclusive
        //  - acquires the lock for write access (only one thread at a time)
        //  - thread that owns exclusive lock MUST NOT try to acquire it again
        //  - the code spins while someone else owns it (not zero) or someone beat us setting it to ExclusivelyOwned in between
        //     - failing fence in interlocked exchange is allowed, first test is just performance optimization (bus locking)
        //     - also YieldProcessor (and all other calls) in Spin function is full memory barrier
        //  - version with timeout parameter returns true on success and false on timeout
        //  - version without timeout parameter always succeeds or blocks forever (use ForceUnlock to recover)
        //
        inline void AcquireExclusive (std::uint32_t * rounds = nullptr) noexcept;

        [[nodiscard]] inline bool AcquireExclusive (std::uint64_t timeout, std::uint32_t * rounds = nullptr) noexcept;

        // AcquireShared
        //  - acquires the lock for read access (multiple threads in parallel, no writter is allowed)
        //  - nesting reader locks is supported as long as number of acquire and release calls is equal
        //  - the call spins while someone exclusively owns the lock, the logic for two tests is the same as for AcquireExclusive
        //  - version with timeout parameter returns true on success and false on timeout
        //  - version without timeout parameter always succeeds or blocks forever (use ForceUnlock to recover)
        //
        inline void AcquireShared (std::uint32_t * rounds = nullptr) noexcept;

        [[nodiscard]] inline bool AcquireShared (std::uint64_t timeout, std::uint32_t * rounds = nullptr) noexcept;

        // ForceUnlock
        //  - use only if the thread/process holding the lock crashed and there is no other reader active
        //
        inline void ForceUnlock () noexcept {
            return this->ReleaseExclusive ();
        }

        // TryUpgradeToExclusive
        //  - attempts to convert shared/reading lock to exclusive/writting
        //  - succeeds only if there are no simultaneous readers
        //
        [[nodiscard]] inline bool TryUpgradeToExclusive () noexcept {
            return this->state == 1
                && InterlockedCompareExchange (&this->state, ExclusivelyOwned, 1) == 1;
        }

        // UpgradeToExclusive
        //  - converts shared/reading lock to exclusive/writting
        //  - call ONLY when holding SINGLE shared lock (after successfull AcquireShared/TryAcquireShared)
        //  - NOTE: using this is almost always a BUG; multiple contending callers will deadlock
        //
        inline void UpgradeToExclusive (std::uint32_t * rounds = nullptr) noexcept;

        [[nodiscard]] inline bool UpgradeToExclusive (std::uint64_t timeout, std::uint32_t * rounds = nullptr) noexcept;

        // DowngradeToShared
        //  - converts exclusive/writting lock to shared/reading (allow others to read, while continuing reading)
        //  - call ONLY when holding exclusive lock, then release using ReleaseShared
        //
        inline void DowngradeToShared () noexcept {
            InterlockedExchange (&this->state, 1);
        }

        // IsLocked
        //  - 
        //
        inline bool IsLocked () const noexcept {
            return this->state != 0;
        }

        // IsLockedExclusively
        //  - 
        //
        inline bool IsLockedExclusively () const noexcept {
            return this->state == ExclusivelyOwned;
        }

    private:
        template <typename Timings>
        inline void Spin (std::uint32_t round);
    };

    // RwSpinLockScopeExclusive
    //  - unlocks exlusive lock acquired through RwSpinLock::exclusively
    //
    class RwSpinLockScopeExclusive {
        friend class RwSpinLock;
        RwSpinLock * lock;

        inline RwSpinLockScopeExclusive (RwSpinLock * lock) noexcept : lock (lock) {};

    public:

        // movable

        inline RwSpinLockScopeExclusive (RwSpinLockScopeExclusive && from) noexcept : lock (from.lock) { from.lock = nullptr; }
        inline RwSpinLockScopeExclusive & operator = (RwSpinLockScopeExclusive && from) noexcept { std::swap (this->lock, from.lock); return *this; }

        // release lock on destruction

        inline ~RwSpinLockScopeExclusive () noexcept;

        // release
        //  - to manually release the exclusive lock before going out of scope
        //  - not checking for null to early catch bugs
        //
        inline void release () noexcept;

        // operator bool
        //  - returns whether the exclusive lock is still active
        //  - enables use in 'if' expression to introduce local scope
        //
        explicit operator bool () const & {
            return this->lock != nullptr;
        }

#ifndef __INTELLISENSE__
        // operator bool, invalid call
        //  - using "if (lock.exclusive ())" is bug -> use "if (auto x = lock.exclusive ())" instead
        //
        explicit operator bool () const && = delete;
#endif
    };

    // RwSpinLockScopeShared
    //  - unlocks exlusive lock acquired through RwSpinLock::shared
    //
    class RwSpinLockScopeShared {
        friend class RwSpinLock;
        RwSpinLock * lock;

        inline RwSpinLockScopeShared (RwSpinLock * lock) noexcept : lock (lock) {};

    public:

        // copyable

        inline RwSpinLockScopeShared (const RwSpinLockScopeShared & from) noexcept;
        inline RwSpinLockScopeShared & operator = (const RwSpinLockScopeShared & from) noexcept;

        // movable

        inline RwSpinLockScopeShared (RwSpinLockScopeShared && from) noexcept : lock (from.lock) { from.lock = nullptr; }
        inline RwSpinLockScopeShared & operator = (RwSpinLockScopeShared && from) noexcept { std::swap (this->lock, from.lock); return *this; }

        // release lock on destruction

        inline ~RwSpinLockScopeShared () noexcept;

        // release
        //  - to manually release the exclusive lock before going out of scope
        //  - not checking for null to early catch bugs
        //
        inline void release () noexcept;

        // operator bool
        //  - returns whether the lock is still active
        //  - enables use in 'if' expression to introduce local scope
        //
        explicit operator bool () const & {
            return this->lock != nullptr;
        }

#ifndef __INTELLISENSE__
        // operator bool, invalid call
        //  - using "if (lock.shared ())" is bug -> use "if (auto x = lock.shared ())" instead
        //
        explicit operator bool () const && = delete;
#endif
    };
}

#include "Windows_RwSpinLock.tcc"
#endif
