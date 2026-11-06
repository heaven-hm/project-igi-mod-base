# IGI 1 native catalogue

The IGI 1 MVP keeps research metadata separate from callable APIs. A native is
callable only when an exposed SDK wrapper's address and signature match the
loaded catalogue and the executable matches the supported build. The manifest
separately records which wrappers passed the Unicorn ABI probe; in-game
effects require live-game validation.

Each record in `data/IGI1/neonix-igi-re-signatures.tsv` contains:

1. the retail executable address;
2. the recovered native name;
3. the source classification;
4. the source kind; and
5. the recovered C-style signature when IGI-RE provides one.

`igi::NativeCatalogue` loads these records and supports lookup by address or
name. Every imported record is initially `Catalogued`. A record must be
independently checked against the supported IGI 1 executable before a future
SDK wrapper may mark it `Verified` or `Exposed`.

The table is intentionally separate from the runtime hook. The scheduler uses
the verified `Game_RunHandler` lifecycle hook. `NativeCaller` resolves by name,
compares the complete IGI-RE signature, rejects catalogued-only records, and
queues verified work on the game thread. Concrete typed calls belong in their
native-domain implementation files rather than in the public caller header.
It never exposes arbitrary memory reads or writes.

The function pointer uses IGI 1's x86 `__cdecl` ABI. A feature may queue a
native only after loading its catalogue record and promoting that exact record
through the verification process; copying a raw address or embedding a native
name/signature in the caller is not supported.
