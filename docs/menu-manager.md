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
# In-memory script resource investigation

IGI-RE identifies `Resource_Register(const char *name, int size, void *data)`
at `0x004B6420`. It creates a user resource, and `Resource_Exists` checks
loaded resources before looking for a file. `Resource_Load` returns the
registered data and its size when the resource name resolves to that entry.
This is a possible route for supplying the generated menu without changing the
game's original QVM files.

The internal demo registers the compiled QVM under an alias-expanded `LOCAL:`
path and retains its backing bytes for the process lifetime. Resource
registration, script callbacks, and task lifecycle still need a live retail
game run before their in-game behavior can be called verified. Shutdown keeps
the runtime resident while live QVM callbacks may still reference DLL state.

IGI-RE's `Resource_IsLoaded` resolves configured aliases with `File_ResolvePath`
before comparing names, while `Resource_Register` stores the name it is given.
`LOCAL:` is a registered device, not necessarily an alias; if no configured
alias matches, `File_ResolvePath` leaves the path unchanged. A matching alias
must be expanded before registration or the resource can be undiscoverable.
`File_ResolvePath` also writes to fixed-size 128-byte buffers; the adapter
checks that the resolved path fits before registering the resource.
