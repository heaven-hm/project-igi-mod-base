# Changelog

## 0.1 — IGI 1 MVP

- Added Windows x86 internal menu demos, configurable hotkeys, logging, and DLL lifecycle management.
- Added game-thread scheduling through display frame callbacks and an IGI-RE native catalogue.
- Added Windows build and injection helpers, Wine launch scripts, and Unicorn native verification.
- Added menu controls for weather, frame rate, weapons, and other native actions.
- Added configurable menu titles, the simple-menu screenshot, and game-constant weapon selection.
- Release builds omit debug sections. Level switching now requests game teardown before starting the selected mission; live verification remains pending.

The supported executable profile is the IGI-RE EU/Chinese IGI 1 build.
