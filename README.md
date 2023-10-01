# R/W Spin Lock
*slim, simple, fast, cross-process, unfair, reader-writer spin lock implementation*

## Features
* size of a single `short`, `long` or `long long` (templated underlying shared lock counter)
* very simple code
* writers don't have priority
* automatically unlocking smart `if` scope guards, see below [Scope guarding](#scope-guarding)
* is inlined yielding additional performance

## Use cases
* sharing lots of separate small pieces of data between processes
* unfair (see below!) locking of very short routines

## Not very suitable
* for high contention scenarios: *backs off from spinning to eventually Sleep(1) which sleeps for LONG*
* for critical sections longer than a few instructions or containing API calls: *suggest use OS primitives instead*
* where reentrancy is required: *this spin lock will not work at all*
* where fair locking strategy is required, see below [Fairness](#fairness)

## Requirements
* **Windows** Vista (due to use of GetTickCount64 function)
* Microsoft Visual Studio `/std:c++17`

## Interface

```cpp
[void/bool] <Try> [Acquire/Release] [Exclusive/Shared] (<timeout, <std::uint32_t * rounds = nullptr>>)
```

* all calls are `noexcept`
* **Try** functions will attempt to lock/upgrade exactly once, without any spinning
* **Try** functions and functions with `timeout` return true/false result, and are `[[nodiscard]]`
* the optional output parameter `rounds` will receive the actual number of spins the operation waited

### Maintenance functions

```cpp
bool IsLocked () const;
bool IsLockedExclusively () const;
void ForceUnlock ();
```

* query functions return immediate state that may have already changed by the time they return
* force-unlocking the lock WILL end up deadlocking or breaking the program if the lock is still in use

### Upgrade/Downgrade

```cpp
bool TryUpgradeToExclusive ();
bool UpgradeToExclusive (std::uint64_t timeout, std::uint32_t * rounds = nullptr);
void DowngradeToShared ();
```

* before attempting to upgrade/downgrade the lock must be in state where this action makes logical sense,
  otherwise the program will deadlock or break
* there is intentionally no UpgradeToExclusive without timeout; usage of those is always a BUG,
  use TryUpgradeToExlusive instead

Crude example of proper usage of lock upgrade:

```cpp
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
```

### Additional members to save typing

```cpp
void acquire () noexcept { AcquireExclusive (); };
void release () noexcept { ReleaseExclusive (); };
```

## Scope guarding
*smart `if` pattern*

Exclusive/Write locking:

```cpp
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
```

Shared/Read locking:
    
```cpp
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
```

Upgrading:

```cpp
if (auto guard = lock.share ()) {
    // read/shared access here
    if (auto guard2 = lock.upgrade ()) {
        // upgraded, can perform write/exclusive access here
    } else {
        // failed to upgrade, someone else is competing for access
    }
    // downgraded back to read/shared access here
}
```

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

| Algorithm | Ryzen 9 5900X | Ryzen 5 1600AF | Xeon Phi 7250 | Snapdragon 835 |
| :--- | ---: | ---: | ---: | ---: |
| CreateMutex | 118 654 ops/s | 78 097 ops/s | 26 285 ops/s | 133 852 ops/s |
| CRITICAL_SECTION | 1 220 984 ops/s | 736 868 ops/s | 277 120 ops/s | 924 668 ops/s |
| SRWLOCK | 12 868 550 ops/s | 3 583 146 ops/s | 1 310 142 ops/s | 4 187 904 ops/s |
| **RwSpinLock** | 56 470 962 ops/s | 26 736 809 ops/s | 3 667 361 ops/s | 15 797 421 ops/s |

### Notes
* Using 16 threads
* Both AMD Ryzens run on **Windows 10 LTSB 2016**
* Xeon Phi 7250 server runs **Windows Server Insider Preview build 25236**
* The Qualcomm Snapdragon 835 laptop runs **Windows 11 22H2 build 25163**
* L2 synchronization bandwidth makes RwSpinLock to appear capped at low percent CPU usage indicated by Task Manager

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

### State variable values
* 0 - unowned/unlocked
* -1 - owned exclusively, for write/modify operations
* +1 and any positive value - number of active shared readers

### Spinning

* TBD: YieldProcessor (n times)
* TBD: SwitchToThread
* TBD: Sleep (0)
* TBD: Sleep (1)
* max 127 spins saves 3 bytes on x86
