# IGI 1 native catalogue

The IGI 1 MVP exposes the native research catalogue as address and signature
metadata. It does not turn an address into a callable function automatically.

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
the verified `Game_RunHandler` lifecycle hook, while native metadata remains
available for later, individually verified wrappers.
