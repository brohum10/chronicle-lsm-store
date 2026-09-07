# Architecture

Chronicle is a compact log-structured merge-tree (LSM-tree) built to make the durability and
read-path tradeoffs of a storage engine visible. It deliberately uses the C++ standard library and
small POSIX durability primitives instead of hiding the core ideas behind a database dependency.

```text
put / delete / atomic batch
      |
      v
+-------------------+       append + optional fsync
| Write-ahead log   | <------------------------------+
+-------------------+                                |
      | replay                                       |
      v                                              |
+-------------------+    threshold reached    +------+------------+
| Skip-list         | ----------------------> | immutable SSTable |
| memtable          |                         | CRC + Bloom + idx |
+-------------------+                         +------+------------+
      ^                                              |
      | newest value                                 | merge
get / range --+------------ merge newest versions ---+
                                                     v
                                              compacted SSTable
```

## Write path

1. `Database` validates every mutation before assigning monotonic sequence numbers.
2. The complete mutation or atomic batch is appended to the write-ahead log. With the default configuration,
   `fsync` completes before the call can succeed.
3. The mutation enters a probabilistic skip list, giving expected `O(log n)` insert and lookup.
4. At the configured size threshold, the already-sorted memtable is written to a temporary SSTable.
5. The SSTable is flushed, synced, atomically renamed, and its parent directory is synced. Only then
   can Chronicle reset the WAL and clear the memtable.

That ordering is the key crash-safety invariant: acknowledged data always exists in the WAL, the
memtable, or an atomically installed SSTable.

## Read path

Reads first check the memtable. Chronicle then checks SSTables from newest to oldest. Each table has
a Bloom filter, so keys that are definitely absent avoid a binary search and disk read. A matching
tombstone stops the search and returns `not found`, preventing an older value from resurfacing.

Ordered range and prefix reads use a min-heap to perform a k-way merge across the sorted memtable and
the relevant slice of every SSTable. When the same key exists in several levels, its greatest sequence
number wins; tombstones are filtered only after version resolution. With `k` sources and `n` visited
entries, merge work is `O(n log k)`.

The current implementation keeps a key-to-offset index in memory and owns one descriptor per open
table. Point and range reads use `pread`, so independent readers do not share or lock a mutable file
cursor. A bounded block cache would be a natural next step for a long-running, read-heavy process.

## Recovery

On startup Chronicle:

1. discovers and validates all `sst-<id>.sst` files;
2. rebuilds their in-memory indexes and Bloom filters;
3. restores the highest committed sequence number; and
4. replays every complete, checksum-valid WAL record.

A partial final WAL record is treated as a torn tail and ignored. A complete checksum-invalid WAL
record and any invalid immutable SSTable fail loudly instead of silently returning questionable data.

## Compaction

Compaction performs a k-way logical merge of all immutable tables. For each key, the entry with the
largest sequence number wins. Tombstones remain in the compacted output because keeping them is the
safe choice if an old file cannot be removed after the replacement is installed. The new table is
made durable before obsolete inputs are deleted.

## Concurrency model

`Database` uses a `std::shared_mutex`: independent point/range readers can proceed together, while
mutations, atomic batches, flushes, and compactions take exclusive ownership. Atomic counters track
reads, writes, Bloom-filter rejections, flushes, compactions, scans, and returned range rows without
expanding the critical section. Chronicle is thread-safe within one process; it does not claim
multi-process writer coordination.
