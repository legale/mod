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

The `uevent` module offers a `check` target which runs both regular and robust tests.

## Root Makefile

The optional Makefile in the repository root aggregates common actions:

- `make test` – run tests for all modules.
- `make coverage` – run coverage for modules that support it.
- `make clean` – remove build artefacts in all modules.

Use it as a convenience wrapper instead of invoking `make` in each subdirectory.
