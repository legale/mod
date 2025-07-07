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

  Many networking helpers rely on the `syslog2` library for logging instead of
  shipping their own implementations.

## Custom Initialization

Each module exposes a `*_mod_init` function that accepts an optional
`mod_init_args_t` structure. Use these hooks to override logging and timing
callbacks before calling any other APIs. Modules fall back to the default
implementations (such as `syslog()` and `clock_gettime()`) when the argument is
`NULL` or when individual fields are unset.

Example initialization order:

```c
mod_init_args_t args = {
  .log = my_log_function,
  .get_time = my_get_time,
};

syslog2_mod_init(&(syslog2_mod_init_args_t){
  .log = args.log,
  .get_time = args.get_time,
});

timeutil_mod_init(&(timeutil_mod_init_args_t){
  .get_time = args.get_time,
});

/* initialize other modules here */
```

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

The timeutil module also accepts a `TEST` variable so you can run a single test:

```sh
cd timeutil
make test TEST=msleep_accuracy
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
