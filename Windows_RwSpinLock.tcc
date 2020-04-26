#ifndef WINDOWS_RWSPINLOCK_TCC
#define WINDOWS_RWSPINLOCK_TCC

// RwSpinLockScopeExclusive

inline Windows::RwSpinLockScopeExclusive::~RwSpinLockScopeExclusive () noexcept {
    if (this->lock) {
        this->release ();
    }
}

inline void Windows::RwSpinLockScopeExclusive::release () noexcept {
    this->lock->ReleaseExclusive ();
    this->lock = nullptr;
}

// RwSpinLockScopeShared

inline Windows::RwSpinLockScopeShared::RwSpinLockScopeShared (const Windows::RwSpinLockScopeShared & from) noexcept : lock (from.lock) { this->lock->AcquireShared (); }
inline Windows::RwSpinLockScopeShared & Windows::RwSpinLockScopeShared::operator = (const Windows::RwSpinLockScopeShared & from) noexcept {
    if (this->lock) {
        this->release ();
    }
    this->lock = from.lock;
    if (this->lock) {
        this->lock->AcquireShared ();
    }
    return *this;
}

inline Windows::RwSpinLockScopeShared::~RwSpinLockScopeShared () noexcept {
    if (this->lock) {
        this->release ();
    }
}

inline void Windows::RwSpinLockScopeShared::release () noexcept {
    this->lock->ReleaseShared ();
    this->lock = nullptr;
}

// RwSpinLock

inline void Windows::RwSpinLock::AcquireExclusive () noexcept {
    std::uint32_t r = 0;
    while (!this->TryAcquireExclusive ()) {
        if (++r <= Parameters::Exclusive::Yields) {
            YieldProcessor ();
        } else {
            this->Spin <Parameters::Exclusive> (r);
        }
    }
}

inline void Windows::RwSpinLock::AcquireShared () noexcept {
    std::uint32_t r = 0;
    while (!this->TryAcquireShared ()) {
        if (++r <= Parameters::Shared::Yields) {
            YieldProcessor ();
        } else {
            this->Spin <Parameters::Shared> (r);
        }
    }
}

inline void Windows::RwSpinLock::UpgradeToExclusive () noexcept {
    std::uint32_t r = 0;
    while (!this->TryUpgradeToExclusive ()) {
        if (++r <= Parameters::Upgrade::Yields) {
            YieldProcessor ();
        } else {
            this->Spin <Parameters::Upgrade> (r);
        }
    }
}

[[nodiscard]] inline bool Windows::RwSpinLock::AcquireExclusive (std::uint64_t timeout) noexcept {
    std::uint32_t r = 0;

    while (!this->TryAcquireExclusive ()) {
        if (++r <= Parameters::Exclusive::Yields) {
            YieldProcessor ();
        } else {
            if (timeout) {
                auto t = GetTickCount64 () + timeout;
                SwitchToThread ();

                // contested case, with backoff
                while (!this->TryAcquireExclusive ()) {
                    if (GetTickCount64 () < t) {
                        this->Spin <Parameters::Exclusive> (++r);
                    } else
                        return false;
                }
            }
            break;
        }
    }
    return true;
}

[[nodiscard]] inline bool Windows::RwSpinLock::AcquireShared (std::uint64_t timeout) noexcept {
    std::uint32_t r = 0;

    while (!this->TryAcquireShared ()) {
        if (++r <= Parameters::Shared::Yields) {
            YieldProcessor ();
        } else {
            if (timeout) {
                auto t = GetTickCount64 () + timeout;
                SwitchToThread ();

                // contested case, with backoff
                while (!this->TryAcquireShared ()) {
                    if (GetTickCount64 () < t) {
                        this->Spin <Parameters::Shared> (++r);
                    } else
                        return false;
                }
            }
            break;
        }
    }
    return true;
}

[[nodiscard]] inline bool Windows::RwSpinLock::UpgradeToExclusive (std::uint64_t timeout) noexcept {
    std::uint32_t r = 0;

    while (!this->TryUpgradeToExclusive ()) {
        if (++r <= Parameters::Upgrade::Yields) {
            YieldProcessor ();
        } else {
            if (timeout) {
                auto t = GetTickCount64 () + timeout;
                SwitchToThread ();

                // contested case, with backoff
                while (!this->TryUpgradeToExclusive ()) {
                    if (GetTickCount64 () < t) {
                        this->Spin <Parameters::Upgrade> (++r);
                    } else
                        return false;
                }
            }
            break;
        }
    }
    return true;
}

// internals

template <typename Timings>
inline void Windows::RwSpinLock::Spin (std::uint32_t round) {
    DWORD n = 0;
    if (round > Timings::Yields + Timings::Sleep0s) {
        n = 1;
    }
    Sleep (n);
}

// if scope

[[nodiscard]] inline Windows::RwSpinLockScopeExclusive Windows::RwSpinLock::exclusively () noexcept {
    this->AcquireExclusive ();
    return this;
}
[[nodiscard]] inline Windows::RwSpinLockScopeExclusive Windows::RwSpinLock::exclusively (std::uint64_t timeout) noexcept {
    if (this->AcquireExclusive (timeout))
        return this;
    else
        return nullptr;
}

[[nodiscard]] inline Windows::RwSpinLockScopeShared Windows::RwSpinLock::share () noexcept {
    this->AcquireShared ();
    return this;
}
[[nodiscard]] inline Windows::RwSpinLockScopeShared Windows::RwSpinLock::share (std::uint64_t timeout) noexcept {
    if (this->AcquireShared (timeout))
        return this;
    else
        return nullptr;
}

#endif
