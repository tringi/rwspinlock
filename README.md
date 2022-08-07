# R/W Spin Lock
*slim, cross-process, reader-writer spin lock implementation*

## Features
* single **long**
* very simple code
* writers don't have priority
* automatically unlocking `if` scope guards, see below

## Use cases
* sharing lots of separate small pieces of data between processes
* unfair locking of very short pieces of code

## Especially NOT suitable:
* for high contention scenarios: *backs off from spinning to eventually Sleep(1) which sleeps for LONG*
* for critical sections longer than a few instructions: *use OS primitives instead*
* where reentrancy is required: *this spin lock will not work at all*
* where fair locking strategy is required

## Requirements
* Windows Vista (due to use of GetTickCount64 function)

## Interface

    [void/bool] <Try> [Acquire/Release/UpgradeTo] [Exclusive/Shared] (<std::uint64_t timeout, <std::uint32_t * rounds = nullptr>>)

* all calls are `noexcept`
* **Try** calls try locking/upgrading exactly once, without spinning
* **Try** calls and calls with `timeout` return true/false result and are `[[nodiscard]]`
* the optional output parameter `rounds` will receive number of spins the call waited

### Extra member functions

    void acquire () noexcept { AcquireExclusive (); };
    void release () noexcept { ReleaseExclusive (); };

    void ForceUnlock ();
    void DowngradeToShared ();
    bool IsLocked () const;
    bool IsLockedExclusively () const;

## Scope guarding
*smart `if` pattern*

    if (auto guard = lock.exclusively ()) {
        // guarded code, now ready for write/exclusive access
        // lock is released on scope exit
    }
    
    if (auto guard = lock.exclusively (1000)) {
        // guarded code, now ready for write/exclusive access
        // lock is released on scope exit
    } else {
        // timeout, someone else holds exclusive access
    }
    
    if (auto guard = lock.share ()) {
        // guarded code, now ready for read/shared access
        // lock is released on scope exit
    }
    
    if (auto guard = lock.share (1000)) {
        // guarded code, now ready for read/shared access
        // lock is released on scope exit
    } else {
        // timeout, someone holds exclusive access
    }

## References
* https://software.intel.com/en-us/articles/implementing-scalable-atomic-locks-for-multi-core-intel-em64t-and-ia32-architectures/

## Performance
There is very crude test program in `Test` directory that measures how many allocate/release
cycles can simple bitmap allocator do, if every operation is locked. **NOTE:** We are measuring
the performance difference of the lock, not the allocator.

Compile and change the `bool custom` parameter to choose the algorithm, or download the EXE and
run it with `srw` (SRWLOCK), `cs` (CRITICAL_SECTION) or `spinlock` (RwSpinLock) parameter.

### Results
*best numbers of dozen 10s runs, high performance power scheme*

| Algorithm | AMD Ryzen 5 1600AF | Snapdragon 835 |
| | Windows 10 LTSB 2016 | Windows 11 build 25163 |
| :--- | ---: | ---: |
| CRITICAL_SECTION | 657 965 ops/s | 858 317 ops/s |
| SRWLOCK | 3 009 137 ops/s | 3 289 008 ops/s |
| RwSpinLock | 26 736 809 ops/s | 15 797 421 ops/s @ 17% CPU |

## Implementation details

TBD: long state
 - 0 - unowned
 - -1 - owned exclusively (for write/modify operations)
 - +1 and above, number of shared readers

### Spinning

* TBD: YieldProcessor (n times)
* TBD: SwitchToThread
* TBD: Sleep (0)
* TBD: Sleep (1)
* max 127 spins saves 3 bytes on x86
