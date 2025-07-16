# C Module Collection

This repository contains several small C libraries and utilities. Each module lives in its own directory with a standalone `Makefile` and unit tests.

## Modules

- **htable** – simple hash table implementation.
- **list** – doubly linked list helpers from the Linux kernel.
- **minheap** – binary min-heap container.
- **netlink_getlink** – query network interfaces via netlink.
- **nlmon** – netlink monitor used for tracking network events.
- **syslog2** – lightweight asynchronous logging helper.
- **timeutil** – helper routines for measuring time and pausing execution.
- **uevent** – event loop implementation built on `epoll`.
- **netlink_arp_cache** – small helper for parsing netlink ARP replies.
- **libpcap-dhcp-capture** – capture DHCP packets using libpcap.
- **hashtable-linux-kernel** – Linux kernel inspired hash table experiments.
- **leak_detector_c** – simple leak detection helper used by other modules.

Many modules embed tiny fallback versions of functions from `syslog2` and
`timeutil`.  These fallbacks are declared with `__attribute__((weak))` so the
real implementations seamlessly override them when available.  When a module is
built as a library only strong symbols are emitted, keeping dependencies
explicit.

## Weak symbol fallbacks

The previous `*_mod_init` entry points were removed.  To customize a module you
now provide your own implementations of the weak symbols it uses.  Linking the
module together with `syslog2` or any other dependency automatically replaces
the stubs shipped in the source.

## Running Tests

Every module directory exposes a `test` target:

```sh
cd <module>
make test
```

Some modules (minheap, netlink_getlink, nlmon, syslog2 and timeutil) also provide a `coverage` target that recompiles with GCOV flags and prints a short coverage report:

```sh
cd <module>
make coverage
```

Every module's `test` target accepts a `TEST` variable so you can run a single
test by name:

```sh
cd <module>
make test TEST=test_name
```

A dedicated performance test named `perf_custom_vs_system` compares the
execution time of the fast helpers with the standard `clock_gettime`
implementation for both monotonic and realtime clocks. The results are
printed in absolute values and percentages:

```sh
cd timeutil
make test TEST=perf_custom_vs_system
```

The `uevent` module offers a `check` target which runs both regular and robust tests.

A small `test_util.h` header at the repository root defines common macros for
colored output in tests. All test programs include it to keep style
consistent.

## Root Makefile

The optional Makefile in the repository root aggregates common actions:

- `make test` – run tests for all modules.
- `make coverage` – run coverage for modules that support it.
- `make clean` – remove build artefacts in all modules.

Use it as a convenience wrapper instead of invoking `make` in each subdirectory.

## CI Script

The `ci.sh` helper script runs `clang-tidy` over all `*.c` and `*.h` files and
then executes the full test suite. Make sure `clang-tidy` is installed before
invoking it:

```sh
./ci.sh
```
