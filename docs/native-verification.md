# IGI 1 native verification policy

The supported profile is the EU/Chinese IGI 1 executable with SHA-256:

`dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01`

The catalogue preserves research without claiming that every record is safe
to invoke. Verification of a native requires, at minimum:

- matching the address in the supported executable;
- confirming the function body and calling convention;
- confirming argument and return types against the recovered implementation;
- checking the observed lifecycle/call context; and
- recording the result and supporting evidence before exposing a wrapper.

The native table is imported from the IGI-RE `names.tsv` address/name source,
with signatures recovered from matching declarations. No IGI-RE function body
is compiled into this SDK. The two CRT records without recovered IGI-RE C
signatures remain catalogued but are not exposed.

The automated executable gate is `tools/verify_igi1_hook.sh`. It verifies the
target hash and the prologues for `Game_RunHandler` (`0x00416260`),
`Flow_RunHandler` (`0x00402260`), and `Game_Update` (`0x00401050`).
