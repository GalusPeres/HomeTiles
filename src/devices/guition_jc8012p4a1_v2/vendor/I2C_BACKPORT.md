# Guition V2 I2C atomic-state allocation

`i2c_master_internal.c` and `i2c_private.h` come from Espressif ESP-IDF
`87912cd291` (Arduino ESP32 3.3.7, IDF 5.5.2). The original Apache-2.0
notices are retained. Only the exact Guition JC8012P4A1 V2 profile compiles
the replacement driver; other profiles continue to link the SDK archive.

The only functional change is the master-bus allocation from upstream
[37758ef327f9](https://github.com/espressif/esp-idf/commit/37758ef327f95959b46f275e1c1ad1d0a08aed12):
allocate the object containing atomic status and transaction-index members
with `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`, never the default PSRAM heap.
The SDK archive's master-driver object is superseded by these definitions.
No global allocator policy, bus clock, GPIO, timeout, or watchdog is changed.

The maintainer's crash dump matches ELF SHA256
`af562d1cea4dc3d8096ec17cd631e45cc6d82ab1ea6fb0037c03e8638c234a67`.
Its startup task is uploading touch firmware with the master-bus object at
`0x483e96bc`, in PSRAM. This establishes exposure to the upstream defect;
it does not prove that defect caused the captured interrupt watchdog.
Hardware validation of this backport remains required.

The regression test reconstructs the original driver and verifies its hash,
so unrelated changes cannot be silently folded into the backport. On an SDK
upgrade, review or remove this replacement instead of changing the version
guard without checking the corresponding upstream driver.
