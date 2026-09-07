# Architecture

Chronicle is a compact log-structured merge-tree (LSM-tree) built to make the durability and
read-path tradeoffs of a storage engine visible. It deliberately uses the C++ standard library and
small POSIX durability primitives instead of hiding the core ideas behind a database dependency.

```text
put / delete
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
get --+---------------- scan newest to oldest -------+
                                                     v
                                              compacted SSTable
```

## Write path

1. `Database` validates key and value limits and assigns a monotonic sequence number.
2. The complete mutation is appended to the write-ahead log. With the default configuration,
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

The current implementation keeps a sparse key-to-offset index in memory and opens the table for a
point read. This keeps ownership and corruption handling straightforward. A block cache and shared
file handles would be natural next steps for a long-running, read-heavy process.

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

`Database` uses a `std::shared_mutex`: independent readers can proceed together, while mutations,
flushes, and compactions take exclusive ownership. Atomic counters track read, write, and Bloom-filter
activity without expanding the critical section. Chronicle is thread-safe within one process; it does
not claim multi-process writer coordination.
