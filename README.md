# Chronicle

[![CI](https://github.com/brohum10/chronicle-lsm-store/actions/workflows/ci.yml/badge.svg)](https://github.com/brohum10/chronicle-lsm-store/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Chronicle is an embedded C++20 key-value store built around a log-structured merge tree. It combines
a CRC-protected write-ahead log, atomic batches, skip-list memtable, Bloom-filtered immutable SSTables,
ordered range reads, tombstones, crash recovery, and compaction in a dependency-free implementation.

This project focuses on the decisions that make a storage engine trustworthy—not only a `map` saved
to a file. Writes follow an explicit durability order, immutable files are atomically installed, torn
WAL tails are recoverable, and corrupt SSTables are rejected before serving data.

## Highlights

- Expected `O(log n)` in-memory reads and writes through a probabilistic skip list
- Configurable synchronous WAL writes with a CRC-32 envelope around every mutation batch
- Atomic multi-key batches: recovery applies the complete batch or none of it
- Sorted SSTables with in-memory offset indexes and 1% target false-positive Bloom filters
- RAII-managed table descriptors and thread-safe positional reads with `pread`
- Half-open range scans and prefix scans using a k-way merge across sorted storage levels
- Monotonic sequence numbers, update semantics, tombstones, recovery, flush, and compaction
- Concurrent readers through `std::shared_mutex` with exclusive mutation and maintenance paths
- Portable little-endian file format with strict structural and checksum validation
- Eleven deterministic tests, including torn batches, corruption, randomized model checking, restarts,
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
database.write_batch({
    {"service:api:status", "healthy"},
    {"service:api:old-owner", std::nullopt},
});

for (const auto& item : database.scan_prefix("service:api:")) {
    // consume item.key and item.value in sorted order
}
database.compact();
```

`sync_writes` defaults to `true`. Benchmarks can disable it to measure the data-structure and table
pipeline separately from the host's durable-storage latency.

## Architecture

```text
                         +----------------+
put / delete / batch --> | CRC WAL + fsync | --------+
                         +----------------+          |
                                                   v
get / range ------> skip-list memtable ----flush----> immutable SSTables
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
8. 5,000 seeded random operations checked against `std::map`;
9. atomic multi-key batch validation, torn-batch rejection, and restart recovery;
10. range/prefix k-way merge behavior across levels, updates, limits, and tombstones; and
11. 1,000 writes issued concurrently by four threads.

Local ASan/UBSan result: **11/11 tests passed** with no sanitizer findings.

## Reproducible benchmark

`make benchmark` performs 100,000 individual writes, 100,000 writes in 100-item atomic batches,
100,000 seeded random reads, 1,000 bounded range scans, and a full compaction. It also compares 2,000
individually synced writes with the same workload grouped into durable batches. A release build on an
Apple Silicon development machine produced these medians across three serial runs:

| Workload | Result |
| --- | ---: |
| Sequential writes | 47,016 ops/s |
| 100-item atomic batch writes | 53,349 ops/s |
| Individually `fsync`-backed writes | 25,451 ops/s |
| `fsync`-backed 100-item batches | 165,175 ops/s |
| Random point reads | 294,173 ops/s |
| 100-row range scans | 3,806 queries/s |
| Full compaction | 0.735 s |
| SSTables afterward | 1 |

These are engineering measurements rather than universal claims: filesystem, compiler, hardware,
and `fsync` settings affect results. The checked-in benchmark prints all workload parameters and a
read checksum so the run can be reproduced and compared.

## Scope and next steps

Chronicle is an educational embedded engine, not a production database. It supports point and bounded
range operations within one in-process writer domain. The most valuable next steps would be streaming
range iterators, block caching, leveled compaction, prefix-compressed index blocks, snapshot reads, and
multi-process file locking.

## License

[MIT](LICENSE) © 2026 Soham Jindal
