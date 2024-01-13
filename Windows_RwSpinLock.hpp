#ifndef WINDOWS_RWSPINLOCK_HPP
#define WINDOWS_RWSPINLOCK_HPP

#include <Windows.h>
#include <algorithm>
#include <cstdint>

namespace Windows {
    template <typename StateType> class RwSpinLockScopeShared;
    template <typename StateType> class RwSpinLockScopeUpgraded;
    template <typename StateType> class RwSpinLockScopeExclusive;

    // RwSpinLock
    //  - slim, cross-process, reader-writer spin lock implementation
    //  - unfair locking, writers don't have priority and can be starved
    //  - StateType - underlying interlocked counter variable
    //     - supported: 'short', 'long' or 'long long'
    //
    template <typename StateType = short>
    class RwSpinLock {

        // state
        //  - 0 - unowned
        //  - -1 - owned exclusively (for write/modify operations)
        //  - +1 and above, number of shared readers
        //
        StateType state = 0;

    private:
        static constexpr StateType ExclusivelyOwned = -1;
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

        [[nodiscard]] inline RwSpinLockScopeExclusive <StateType> exclusively (std::uint32_t * rounds = nullptr) noexcept;
        [[nodiscard]] inline RwSpinLockScopeExclusive <StateType> exclusively (std::uint64_t timeout, std::uint32_t * rounds = nullptr) noexcept;

        [[nodiscard]] inline RwSpinLockScopeUpgraded <StateType> upgrade () noexcept;
        [[nodiscard]] inline RwSpinLockScopeUpgraded <StateType> upgrade (std::uint64_t timeout, std::uint32_t * rounds = nullptr) noexcept;

        [[nodiscard]] inline RwSpinLockScopeShared <StateType> share (std::uint32_t * rounds = nullptr) noexcept;
        [[nodiscard]] inline RwSpinLockScopeShared <StateType> share (std::uint64_t timeout, std::uint32_t * rounds = nullptr) noexcept;

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
                && this->LockedCompareExchange (&this->state, ExclusivelyOwned, 0) == 0;
        }

        // TryAcquireShared
        //  - attempts to acquire shared/read lock, returns result
        //
        [[nodiscard]] inline bool TryAcquireShared () noexcept {
            auto s = this->state;
            return s != ExclusivelyOwned
                && this->LockedCompareExchange (&this->state, s + 1, s) == s;
        }

        // ReleaseExclusive
        //  - releases all and any locks
        //
        inline void ReleaseExclusive () noexcept {
            this->LockedExchange (&this->state, 0);
        }

        // ReleaseShared
        //  - releases one shared/read lock
        //
        inline void ReleaseShared () noexcept {
            this->LockedDecrement (&this->state);
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
                && this->LockedCompareExchange (&this->state, ExclusivelyOwned, 1) == 1;
        }

        // UpgradeToExclusive
        //  - converts shared/reading lock to exclusive/writting
        //  - call ONLY when holding SINGLE shared lock (after successfull AcquireShared/TryAcquireShared)
        //
        [[nodiscard]] inline bool UpgradeToExclusive (std::uint64_t timeout, std::uint32_t * rounds = nullptr) noexcept;

        // DowngradeToShared
        //  - converts exclusive/writting lock to shared/reading (allow others to read, while continuing reading)
        //  - call ONLY when holding exclusive lock, then release using ReleaseShared
        //
        inline void DowngradeToShared () noexcept {
            this->LockedExchange (&this->state, 1);
        }

        // IsLocked
        //  - returns true if the lock is currently locked, either for shared or exclusive access
        //  - returns immediate state that may have already changed by the time the call returns
        //
        inline bool IsLocked () const noexcept {
            return this->state != 0;
        }

        // IsLockedExclusively
        //  - returns true if the lock is currently exclusively locked
        //  - returns immediate state that may have already changed by the time the call returns
        //
        inline bool IsLockedExclusively () const noexcept {
            return this->state == ExclusivelyOwned;
        }

    private:
        template <typename Timings>
        inline void Spin (std::uint32_t round);

        inline long long LockedCompareExchange (volatile long long * dst, long long x, long long cmp) noexcept { return InterlockedCompareExchange64 (dst, x, cmp); }
        inline     short LockedCompareExchange (volatile     short * dst,     short x,     short cmp) noexcept { return InterlockedCompareExchange16 (dst, x, cmp); }
        inline      long LockedCompareExchange (volatile      long * dst,      long x,      long cmp) noexcept { return InterlockedCompareExchange   (dst, x, cmp); }

        inline long long LockedExchange (volatile long long * dst, long long cmp) noexcept { return InterlockedExchange64 (dst, cmp); }
        inline     short LockedExchange (volatile     short * dst,     short cmp) noexcept { return InterlockedExchange16 (dst, cmp); }
        inline      long LockedExchange (volatile      long * dst,      long cmp) noexcept { return InterlockedExchange   (dst, cmp); }

        inline long long LockedDecrement (volatile long long * dst) noexcept { return InterlockedDecrement64 (dst); }
        inline     short LockedDecrement (volatile     short * dst) noexcept { return InterlockedDecrement16 (dst); }
        inline      long LockedDecrement (volatile      long * dst) noexcept { return InterlockedDecrement   (dst); }
    };

    // RwSpinLockScopeExclusive
    //  - unlocks exclusive lock acquired through RwSpinLock::exclusively
    //
    template <typename StateType>
    class RwSpinLockScopeExclusive {
        friend class RwSpinLock <StateType>;
        RwSpinLock <StateType> * lock;

        inline RwSpinLockScopeExclusive (RwSpinLock <StateType> * lock) noexcept : lock (lock) {};

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
        //  - using "if (lock.exclusively ())" is bug -> use "if (auto x = lock.exclusively ())" instead
        //
        explicit operator bool () const && = delete;
#endif
    };

    // RwSpinLockScopeUpgraded
    //  - downgrades exclusive lock acquired through RwSpinLock::upgrade
    //
    template <typename StateType>
    class RwSpinLockScopeUpgraded {
        friend class RwSpinLock <StateType>;
        RwSpinLock <StateType> * lock;

        inline RwSpinLockScopeUpgraded (RwSpinLock <StateType> * lock) noexcept : lock (lock) {};

    public:

        // movable

        inline RwSpinLockScopeUpgraded (RwSpinLockScopeUpgraded && from) noexcept : lock (from.lock) { from.lock = nullptr; }
        inline RwSpinLockScopeUpgraded & operator = (RwSpinLockScopeUpgraded && from) noexcept { std::swap (this->lock, from.lock); return *this; }

        // downgrade lock on destruction

        inline ~RwSpinLockScopeUpgraded () noexcept;

        // release
        //  - to manually release the exclusive lock before going out of scope
        //  - not checking for null to early catch bugs
        //
        inline void release () noexcept;

        // operator bool
        //  - returns whether the upgraded exclusive lock is still active
        //  - enables use in 'if' expression to introduce local scope
        //
        explicit operator bool () const & {
            return this->lock != nullptr;
        }

#ifndef __INTELLISENSE__
        // operator bool, invalid call
        //  - using "if (lock.upgrade  ())" is bug -> use "if (auto x = lock.upgrade ())" instead
        //
        explicit operator bool () const && = delete;
#endif
    };

    // RwSpinLockScopeShared
    //  - unlocks shared lock acquired through RwSpinLock::shared
    //
    template <typename StateType>
    class RwSpinLockScopeShared {
        friend class RwSpinLock <StateType>;
        RwSpinLock <StateType> * lock;

        inline RwSpinLockScopeShared (RwSpinLock <StateType> * lock) noexcept : lock (lock) {};

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
        //  - using "if (lock.share ())" is bug -> use "if (auto x = lock.share ())" instead
        //
        explicit operator bool () const && = delete;
#endif
    };
}

#include "Windows_RwSpinLock.tcc"
#endif
