# R/W Spin Lock
*slim, cross-process, reader-writer spin lock implementation*

## Features
* single **long**
* writers don't have priority
* TBD

## Especially NOT suitable:
* for high contention scenarios: *backs off from spinning to eventually Sleep(1) which sleeps for LONG*
* for critical sections longer than a few instructions: *use OS primitives instead*
* where reentrancy is required: *this spin lock will not work at all*
* where fair locking strategy is required

## References
* https://software.intel.com/en-us/articles/implementing-scalable-atomic-locks-for-multi-core-intel-em64t-and-ia32-architectures/
* TBD

## Interface

    [**void**/**bool**] <Try> [Acquire/Release/UpgradeTo/DowngradeTo] [Exclusive/Shared] (<std::uint64_t timeout>)


* void AcquireExclusive ()
* bool AcquireExclusive (timeout)
* void AcquireShared ()
* bool AcquireShared (timeout)

* void ReleaseExclusive ()
* void ReleaseShared ()
* void ForceUnlock () -> ReleaseExclusive ()

* bool TryAcquireExclusive 
* bool TryAcquireExclusive (timeout)
* bool TryAcquireShared
* bool TryAcquireShared (timeout)
* bool TryUpgradeToExclusive ()
* bool TryUpgradeToExclusive (timeout)

* void UpgradeToExclusive ()
* bool UpgradeToExclusive (timeout)
* void DowngradeToShared ()

## Scoped locking
*smart **if** pattern*

TODO: if (auto guard = lock.exclusively ()) { ... }


## Implementation details

TBD: long state
 - 0 - unowned
 - -1 - owned exclusively (for write/modify operations)
 - +1 and above, number of shared readers

### Spinning

TBD: YieldProcessor (n times)
TBD: SwitchToThread
TBD: Sleep (0)
TBD: Sleep (1)

