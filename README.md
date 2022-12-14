# R/W Spin Lock
*slim, simple, fast, cross-process, unfair, reader-writer spin lock implementation*

## Features
* single **short**, **long** or **long long** (templated underlying shared lock counter)
* very simple code
* writers don't have priority
* automatically unlocking `if` scope guards, see below

## Use cases
* sharing lots of separate small pieces of data between processes
* unfair (see below!) locking of very short pieces of code

## Especially NOT suitable:
* for high contention scenarios: *backs off from spinning to eventually Sleep(1) which sleeps for LONG*
* for critical sections longer than a few instructions: *use OS primitives instead*
* where reentrancy is required: *this spin lock will not work at all*
* where fair locking strategy is required (see below!)

## Requirements
* **Windows** Vista (due to use of GetTickCount64 function)
* Microsoft Visual Studio `/std:c++17`

## Interface

    [void/bool] <Try> [Acquire/Release] [Exclusive/Shared] (<timeout, <std::uint32_t * rounds = nullptr>>)

* all calls are `noexcept`
* **Try** functions will attempt to lock/upgrade exactly once, without any spinning
* **Try** functions and functions with `timeout` return true/false result, and are `[[nodiscard]]`
* the optional output parameter `rounds` will receive the actual number of spins the operation waited

### Maintenance functions

    bool IsLocked () const;
    bool IsLockedExclusively () const;
    void ForceUnlock ();

* query functions return immediate state that may have already changed by the time they return
* force-unlocking the lock WILL end up deadlocking or breaking the program if the lock is still in use

### Upgrade/Downgrade

    bool TryUpgradeToExclusive ();
    bool UpgradeToExclusive (std::uint64_t timeout, std::uint32_t * rounds = nullptr);
    void DowngradeToShared ();

* before attempting to upgrade/downgrade the lock must be in state where this action makes logical sense,
  otherwise the program will deadlock or break
* there is intentionally no UpgradeToExclusive without timeout; usage of those is always a BUG,
  use TryUpgradeToExlusive instead

Crude example of proper usage of lock upgrade:

    void DataInsertionProcedure (Item x) {
        while (true) {
            lock.AcquireShared ();
            auto place = FindInsertionPlace (x); // traverse structure to find proper insertion place
    
            if (lock.TryUpgradeToExclusive ()) {
                InsertDataToPlace (place, x); // datamodification operation
    
                lock.ReleaseExclusive ();
                return;
            }
            lock.ReleaseShared ();
        }
    }

### Additional members to save typing

    void acquire () noexcept { AcquireExclusive (); };
    void release () noexcept { ReleaseExclusive (); };

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

Compile and change the `algorithm` variable to choose the algorithm, or download the EXE and run it with
`srw` (SRWLOCK), `cs` (CRITICAL_SECTION), `mutex` (CreateMutex API) or `spinlock` (RwSpinLock) parameter.

### Results
*best numbers of dozen 10s runs, high performance power scheme*

| Algorithm | AMD Ryzen 5 1600AF | Xeon Phi 7250 | Snapdragon 835 |
| :--- | ---: | ---: | ---: |
| CreateMutex | 78 097 ops/s | 26 285 ops/s | 133 852 ops/s |
| CRITICAL_SECTION | 736 868 ops/s | 277 120 ops/s | 924 668 ops/s |
| SRWLOCK | 3 583 146 ops/s | 1 310 142 ops/s | 4 187 904 ops/s |
| **RwSpinLock** | 26 736 809 ops/s | 3 667 361 ops/s | 15 797 421 ops/s |

### Notes
* Using 16 threads
* AMD Ryzen 5 1600AF computer runs **Windows 10 LTSB 2016**
* Xeon Phi 7250 server runs **Windows Server Insider Preview build 25236**
* The Qualcomm Snapdragon 835 laptop runs **Windows 11 22H2 build 25163**
* RwSpinLock seems to be somehow capped at ~17% CPU as indicated by Task Manager, I'm investigating...

### Fairness
On the Xeon Phi machine above, I've run test with 256 threads competing over 1kB bitmap, comparing how
many locks each thread managed to aquire. The test run for about 2 minutes.
The following are ratios between the most and the least successful thread.

| CreateMutex | CRITICAL_SECTION | SRWLOCK | **RwSpinLock** |
| ---: | ---: | ---: | ---: |
| 1.25 | 1.15 | 1.09 | 12.84 |

These results show significant unfairness of the RwSpinLock and it's unsuitability for constantly
highly contended resources.

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
