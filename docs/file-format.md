# On-disk format

All integers are unsigned and little-endian. Length prefixes and CRC-32 checksums make records
self-delimiting and allow recovery to reject torn or corrupted data.

## Write-ahead log

```text
+------------------+----------------------+------------------+
| payload bytes u32| mutation payload     | payload CRC32 u32|
+------------------+----------------------+------------------+
```

Each WAL payload is one atomic batch:

```text
record type u8 | entry count u32 | (entry bytes u32 | mutation payload)...
```

Each mutation payload is:

```text
tombstone u8 | sequence u64 | key bytes u32 | value bytes u32 | key | value
```

Recovery processes records in order. It ignores an incomplete final record, which can result from a
power loss during an append. Because an entire batch shares one checksum envelope, recovery applies
either every mutation in that batch or none of them. Oversized, malformed, or checksum-invalid
complete records fail loudly so corruption is not mistaken for an ordinary torn write.

## SSTable

```text
magic u32 | version u32 | entries u64 | max sequence u64
Bloom bits u64 | Bloom hashes u32 | Bloom words u64 | words...
record 0 | record 1 | ... | record n-1
```

Each record uses the same length/payload/CRC envelope as the WAL. Keys must be strictly increasing.
When a table opens, Chronicle verifies the header, Bloom-filter dimensions, every record checksum,
key ordering, trailing bytes, and the recorded maximum sequence number before accepting it.

The format currently has version `1`. Unknown versions are rejected, leaving room to introduce
compression, index blocks, or alternative checksum algorithms without ambiguous reads.
