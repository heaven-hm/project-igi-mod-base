# MenuManager

`igi::MenuManager` owns renderer-independent menu state and exposes immutable
snapshots for a future renderer. Input changes visual state synchronously;
feature callbacks are queued through `igi::Scheduler` and run only from the
verified game-thread hook.

## Input repeat

Use `updateInput(input, elapsed)` for polled directional input. The first
directional input is applied immediately. Holding the same direction waits
300 milliseconds before the first repeat, then repeats every 100 milliseconds.
Changing input starts a new repeat sequence. Visibility, confirm, back, and
toggle-visibility inputs are handled once per update and are not repeated.

## Shutdown

`shutdown()` is terminal for the manager's input path. It clears menus and
their callbacks, clears submenu and repeat state, hides the menu, and causes
later `handleInput` and `updateInput` calls to be rejected or ignored.
