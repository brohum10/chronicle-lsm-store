# Contributing

Chronicle keeps the storage core small enough to reason about, so changes should preserve that
property. Before opening a pull request:

```bash
make test
make sanitize
make benchmark
```

New storage behavior should include a deterministic failure or recovery test. Changes to the on-disk
format must bump its version and update `docs/file-format.md`; benchmark claims should include the
workload, compiler mode, durability setting, and a checksum that prevents dead-code elimination.

Please keep public APIs focused, compile with warnings treated as errors, and document any crash-order
or concurrency invariant that the change introduces.
