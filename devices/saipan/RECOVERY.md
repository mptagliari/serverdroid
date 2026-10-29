# Recovery and proposed flash plan

**Not prepared. No flashing is authorized or performed by this workflow.**

This submission documents research gates only; it proposes no partition write (`safety.proposed_writes` is empty) and asserts no partition as
protected (an empty list means not assessed here, never that a partition is safe to touch). The owner's own recovery practice for this unit
(holding Power, then Power with Volume Down to reach fastboot, and flashing the stock `boot.img` from the device's own firmware) is not
reproduced here because it depends on firmware the owner holds privately and has not been reviewed for public redistribution. A real flash plan
for this device needs device-specific partition-map sources, backups, exact targets and recovery without working Linux, network or display, plus
owner review and authorization outside the public contribution workflow.
