# Security Policy

Chronicle reads and writes binary WAL and SSTable files. Reports involving memory safety, corruption acceptance, path traversal, denial of service from malformed files, or durability violations should be reported privately.

Use GitHub's **Security → Report a vulnerability** flow when available. Otherwise, contact the maintainer through the GitHub profile. Include the compiler and platform, a minimal reproducer or file, the observed impact, and sanitizer output when available.

The current `main` branch is supported. Chronicle is an educational embedded engine; applications remain responsible for access control, encryption, backups, and process isolation.
