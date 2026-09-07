# Chronicle

[![CI](https://github.com/brohum10/chronicle-lsm-store/actions/workflows/ci.yml/badge.svg)](https://github.com/brohum10/chronicle-lsm-store/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Chronicle is an embedded C++20 key-value store built around a log-structured merge tree. It combines
a CRC-protected write-ahead log, skip-list memtable, Bloom-filtered immutable SSTables, tombstones,
crash recovery, and full-table compaction in a dependency-free implementation.

This project focuses on the decisions that make a storage engine trustworthy—not only a `map` saved
to a file. Writes follow an explicit durability order, immutable files are atomically installed, torn
WAL tails are recoverable, and corrupt SSTables are rejected before serving data.

## Highlights

- Expected `O(log n)` in-memory reads and writes through a probabilistic skip list
- Configurable synchronous WAL writes with a CRC-32 envelope around every mutation
- Sorted SSTables with in-memory offset indexes and 1% target false-positive Bloom filters
- Monotonic sequence numbers, update semantics, tombstones, recovery, flush, and compaction
- Concurrent readers through `std::shared_mutex` with exclusive mutation and maintenance paths
- Portable little-endian file format with strict structural and checksum validation
- Nine deterministic tests, including torn writes, corruption, randomized model checking, restarts,
  and concurrent writers
- CMake and Make builds, cross-platform GitHub Actions, and ASan/UBSan verification

## Quick start

Requirements: a C++20 compiler and either Make or CMake. Chronicle has no third-party runtime or test
dependencies.

```bash
make test
make sanitize
make benchmark
make all
./build/chronicle_cli ./demo-data
```

Equivalent CMake workflow:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Library API

```cpp
#include "chronicle/database.hpp"

chronicle::Database database("./data");
database.put("deployment", "healthy");

if (const auto value = database.get("deployment")) {
    // use *value
}

database.erase("deployment");
database.compact();
```

`sync_writes` defaults to `true`. Benchmarks can disable it to measure the data-structure and table
pipeline separately from the host's durable-storage latency.

## Architecture

```text
                         +----------------+
put / delete ----------> | CRC WAL + fsync | --------+
                         +----------------+          |
                                                   v
get -------------> skip-list memtable ----flush----> immutable SSTables
  ^                        |                         | Bloom filter
  |                        + newest value wins <----+ offset index
  |                                                  CRC records
  +------------------- newest to oldest ----------------+
                                      |
                                      +---- compaction ----> one sorted table
```

The full design, failure ordering, read path, recovery behavior, and concurrency model are described
in [Architecture](docs/architecture.md). Binary layouts are documented in
[On-disk format](docs/file-format.md).

## Verification

The test executable covers:

1. skip-list ordering, replacement, and stale-sequence rejection;
2. Bloom-filter false-negative and false-positive behavior;
3. recovery from a deliberately truncated WAL record;
4. SSTable round trips and deliberate checksum corruption;
5. CRUD semantics and input validation;
6. restart recovery before a memtable flush;
7. flush, compaction, deletion, and restart invariants;
8. 5,000 seeded random operations checked against `std::map`; and
9. 1,000 writes issued concurrently by four threads.

Local ASan/UBSan result: **9/9 tests passed** with no sanitizer findings.

## Reproducible benchmark

`make benchmark` performs 100,000 writes of 128-byte values, 100,000 seeded random reads, and a full
compaction. A release build on an Apple Silicon development machine produced these medians across
three serial runs:

| Workload | Result |
| --- | ---: |
| Sequential writes | 18,409 ops/s |
| Random point reads | 44,031 ops/s |
| Full compaction | 2.534 s |
| SSTables afterward | 1 |

These are engineering measurements rather than universal claims: filesystem, compiler, hardware,
and `fsync` settings affect results. The checked-in benchmark prints all workload parameters and a
read checksum so the run can be reproduced and compared.

## Scope and next steps

Chronicle is an educational embedded engine, not a production database. It supports point operations
and one in-process writer domain. The most valuable next steps would be range scans, block caching,
leveled compaction, prefix-compressed index blocks, snapshot reads, and multi-process file locking.

## License

[MIT](LICENSE) © 2026 Soham Jindal
