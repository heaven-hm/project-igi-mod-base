# Neonix IGI-RE catalogue provenance

`neonix-igi-re-names.tsv` is the unmodified native-name/address catalogue
downloaded from:

`https://github.com/neonixmoddingteam/IGI-RE/blob/main/symbols/names.tsv`

SHA-256: `8655624f9cc3f2e151cb8902ab95f33203d2eae9007ec430cd94850aca80d69f`

`neonix-igi-re-signatures.tsv` is generated with
`tools/import_igi_re_catalogue.py` from that table and the matching IGI-RE
`src/` declarations/definitions. It contains only address, name, source,
kind, and normalized function declaration metadata; no IGI-RE implementation
code is imported.

Imported rows remain `Catalogued` research metadata. Separately,
`mvp-300.tsv` lists exactly 300 generated SDK wrappers. Its `source_only`
rows have matching recovered declarations and compile-time wrappers;
`unicorn_abi` rows also pass the x86 SDK wrapper ABI fixture. Neither status
claims the original gameplay function body or in-game effect was tested.

The table has 5,391 function entries. The source import currently recovers
5,389 signatures; the two remaining entries (`_fsopen` and `_output`) are
Microsoft C runtime routines for which IGI-RE deliberately provides no C
rewrite/declaration. They remain catalogued and are never exposed by this SDK.

`tools/generate_igi_hash.py` converts `neonix-igi-re-names.tsv` into the
generated include `igi_sdk/natives/igi_re/hashes.hpp`: one
`inline constexpr NativeHash <Name> = 0x<address>;` per symbol under
`igi::re::HASH`. CMake regenerates it whenever the TSV changes. The umbrella
`igi_sdk/IGI.hpp` re-exports the same names under `IGI::HASH`.
