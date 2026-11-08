# Verified Mod Helpers Design

## Goal

Extend `demos/helpers/menu_helpers` with reusable, game-thread-only helpers for
common single-player mod actions. Every helper must use the existing typed
`IGI::` wrapper and preserve the retail call shape evidenced by IGI-RE. A
helper must reject unavailable game state instead of fabricating task pointers,
message layouts, AI contexts, or raw offsets.

## Public helper groups

### Player and weapon helpers

- Resolve the active player through `IGI::GAME::GET_PLAYER(0)`.
- Enable unlimited ammunition through `IGI::PLAYER::SET_UNLIMITED_AMMO()`.
- Cycle the current player weapon through
  `IGI::PLAYER::CYCLE_WEAPON(HumanPlayer*, force)`.
- Request a player weapon animation through
  `IGI::PLAYER::REQUEST_WEAPON_ANIM(HumanPlayer*, animationId)`.
- Restore the retail hit/flash feedback through
  `IGI::PLAYER::FADE_HIT_FLASH(HumanPlayer*)` and
  `IGI::PLAYER::FADE_FLASH(HumanPlayer*)`.

Weapon pickup and direct inventory mutation are excluded until IGI-RE supplies
the retail message structure passed to `IGI::WEAPON::PICK_UP_WEAPON`; a helper
must not guess that ABI.

### Game and world helpers

- Keep the existing weather lifecycle, FPS, level-flow, audio, view, and menu
  refresh helpers.
- Add bounded volume helpers that preserve the retail `0.0f..1.0f` input range
  before calling the existing `IGI::AUDIO` wrappers.
- Add player-input and player-look toggles through the typed `IGI::HUMAN`
  wrappers, only when a live player exists on the game thread.

### AI helpers

AI actions are script-context operations. The first helper API will expose
only AI settings whose typed wrapper has no fabricated target pointer:
`IGI::AI::SET_VIEW_LENGTH`, `SET_ALARM_VIEW_LENGTH`, `SET_VIEW_ALPHA`,
`SET_VIEW_GAMMA`, and their secondary-view variants. Each helper documents
that it is valid only while the game invokes an AI event script. Generic menu
code must return a context error rather than invoke an AI action outside that
context.

Targeted patrol/combat/navigation helpers are a second slice after IGI-RE
verifies how a selected soldier is bound to the retail AI context and how node,
task, and event arguments are sourced. This prevents unsafe cross-entity
control while retaining a clear expansion path.

## API and safety rules

- Helpers return `bool`; `false` means no native was called when required game
  state or context was missing.
- All helpers call `requireLiveGameThread` before invoking a game native.
- Player helpers resolve the player through `GAME::GET_PLAYER(0)` once and
  cast only to the typed wrapper parameter expected by the existing binding.
- Numeric values are clamped at the helper boundary where the retail catalog
  documents a range. Animation IDs, force flags, and AI values are passed
  unchanged only when their documented domain is known; otherwise the helper
  is not added.
- Helpers provide a concise log entry and, where appropriate, reuse the retail
  status-message/menu-refresh path.

## Verification seams

1. **Source contract tests:** assert that each public helper uses the intended
   typed `IGI::` wrapper and contains the required game-thread/state guard.
2. **Native harness:** scan each helper call against the retail executable;
   Unicorn validates calls supported by synthetic state and marks live-only
   calls as deferred rather than passing them spuriously.
3. **Windows build:** build internal and ImGui advanced menu DLLs with
   `tools/build-win32-dll.sh`.
4. **Live liveness:** inject each rebuilt DLL through `run_igi_wine.py` and
   confirm process survival after the configured menu key. This demonstrates
   injection safety, not semantic success of a live-only action.

## Implementation slices

1. Add contract tests for the player, input/look, audio, and AI-context APIs.
2. Implement player and game/world helpers to turn those tests green.
3. Validate wrapper/catalog evidence in IGI-RE and run the harness.
4. Add documented AI-context helpers only after their evidence review.
5. Build, inject, review, and submit one focused PR from this branch.

## Non-goals

- No raw memory offsets in the helper API.
- No guessed weapon-pickup message ABI, health layout, soldier layout, or AI
  context binding.
- No live-game success claim based solely on compilation or emulation.
