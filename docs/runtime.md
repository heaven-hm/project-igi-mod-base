# IGI 1 runtime lifecycle

The Windows x86 DLL exports three C ABI entry points:

```cpp
BOOL WINAPI IGI_SDK_Initialize();
void WINAPI IGI_SDK_Shutdown();
unsigned long long WINAPI IGI_SDK_GetGameHookInvocationCount();
```

Call `IGI_SDK_Initialize` only after the DLL has finished loading. It validates
the supported IGI 1 executable, installs the guarded `Game_RunHandler`
trampoline, and leaves the original handler execution intact. Call
`IGI_SDK_Shutdown` before unloading the DLL; shutdown restores the original
bytes, clears scheduled work, and resets the game-thread binding for a later
load.

## Measuring target cadence

Record the counter, wait a measured one-second interval, and record it again:

```text
delta = IGI_SDK_GetGameHookInvocationCount() - start
```

Run the sample during active gameplay on the supported EU/Chinese executable.
The IGI-RE source registers `GAME_FREQUENCY` as 30, so a stable active-game
sample should be compared with approximately 30 handler entries per second.
The counter is diagnostic evidence; it does not alter scheduling or select a
different hook.

The SDK never uses `Draw`, `MenuManager_Draw`, or `TextPrintDetour()` as its
gameplay execution point.
