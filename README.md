# Haunting Ground Recompilation

A native Windows version of Haunting Ground (USA, SLUS-210.75). The original PS2 code is
translated to native code at build time and runs with OpenGL graphics. It is not an
emulator.

**No game files are included.** You need your own copy of the game.

## Status

Playable, but unfinished and not yet played start to finish. Known issues:

- Right-channel music static can follow some music starts.
- Multiply, divide and square root are not yet exact in the last bit.
- Only Windows is currently verified.

## Build

You need Python 3.11+, CMake 3.24+, Visual Studio with C++, and OpenGL.

1. Copy your game into a folder named `Haunting Ground (USA)` in the repo root.
2. Generate the translated code, using the steps in [docs/BUILDING.md](docs/BUILDING.md).
3. Build:

```sh
cmake -S . -B build -DHG_FETCH_GLFW=ON -DHG_TRANSLATED_SOURCE=<repo>/out/translated.cpp -DHG_IOP_SOURCE=<repo>/out/iop-translated.cpp
cmake --build build --config Release --target hg_game
```

## Controls

| Key | Button | Key | Button |
|---|---|---|---|
| WASD | Left stick | Space | Cross |
| IJKL | Right stick | X / C / V | Circle / Square / Triangle |
| Arrows | D-pad | Enter / Tab | Start / Select |
| Q / E | L1 / R1 | 1 / 3 | L2 / R2 |

Gamepads work too. Press F1 in the game for the full list.

## More

- [docs/BUILDING.md](docs/BUILDING.md): detailed build, analysis and verification notes
- [docs/PROGRESS.md](docs/PROGRESS.md): current state and evidence
- [docs/WORLD_EDITOR.md](docs/WORLD_EDITOR.md): the standalone world editor
