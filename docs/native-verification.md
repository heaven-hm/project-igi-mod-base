# IGI 1 native verification policy

The supported profile is the EU/Chinese IGI 1 executable with SHA-256:

`dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01`

The imported research catalogue remains separate from the MVP wrapper set.
The local `data/igi-natives.csv` contains the IGI-RE native catalogue used to generate the x86 `__cdecl` wrappers.
Its validation status distinguishes source declarations from the small ABI
probe. A wrapper build alone does not prove gameplay behavior. Verification
of a native requires, at minimum:

- matching the address in the supported executable;
- confirming the function body and calling convention;
- confirming argument and return types against the recovered implementation;
- checking the observed lifecycle/call context; and
- recording the result and supporting evidence before claiming verified
  gameplay behavior.

The native table is imported from the IGI-RE `names.tsv` address/name source,
with signatures recovered from matching declarations. No IGI-RE function body
is compiled into this SDK. The two CRT records without recovered IGI-RE C
signatures remain catalogued but are not exposed.

## Demo native behavior requiring care

- `Game_SetRaining` (`0x00416110`) is a rain-object callback: IGI-RE reads
  `weather[1]` into the game's raining flag. Passing a menu label such as
  `"rain"` or `"snow"` does not implement weather selection. The actual
  weather resource and its lifecycle still need investigation.
- `HumanPlayer_SetUnlimitedAmmo` (`0x00410360`) sets the unlimited-ammo flag
  to one and takes no arguments. It does not provide a disable operation;
  an on/off toggle must not pretend this native can reverse its effect.
- `HumanView_SetCamera` (`0x004658d0`) takes a live `HumanView*`, a position
  array of doubles and a `Matrix*`. A camera menu needs a verified way to
  obtain and retain the view and matrix; passing arbitrary addresses is not
  a supported substitute.
- `Music_SetVolume` (`0x00495e70`) takes two floats. The similarly named
  `Game_SetMusicVolume` is a script callback with three script arguments,
  not a one-float C function.

These are source-level observations, not successful in-game tests. The demo
menu exposes controls with an implemented adapter. Its weather adapter passes
`{'r', 1, '\0'}` for on and `{'r', 0, '\0'}` for off, matching IGI-RE's
`weather[1]` read. The ammo control is a one-way action. Free camera,
invulnerability, speed, and weapon-grant controls are outside this MVP menu.

The automated executable gate is `tools/verify_igi_hook.sh`. It verifies the
target hash and the prologues for `Game_RunHandler` (`0x00416260`),
`Flow_RunHandler` (`0x00402260`), and `Game_Update` (`0x00401050`).

## Small Unicorn ABI probe

`tests/test_sdk_native_wrappers_unicorn.py` imports IGI-RE's existing
`tests/emu.py`, loads the retail x86 PE and SDK fixture, and probes the ten
gameplay natives used by the menu plus the `Game_Get` and `Task_RunScript`
runtime helpers. IGI-RE's emulator replaces each entry with its own
call logger, then checks the address, cdecl argument words, pointer payloads,
return register, and caller stack cleanup. It does not execute the original
function bodies or verify gameplay effects. The other 288 wrappers are
generated from recovered IGI-RE declarations and compile into the SDK; they
remain source-only until individually validated.

Run it with an x86 Python environment that has `unicorn` and `pefile`:

```sh
IGI_EXE=/path/to/igi.exe \
IGI_RE_ROOT=/path/to/IGI-RE \
IGI_SDK_TEST_DLL=/path/to/igi-sdk-native-abi-fixture.dll \
python3 tests/test_sdk_native_wrappers_unicorn.py
```

CTest marks the probe skipped (rather than passed) when the executable is not
configured or the host is arm64 macOS. The current arm64 Python installation
cannot load an x86_64 Unicorn library, so run the probe under a compatible
x86_64 Python/Rosetta or Windows/Linux environment.
