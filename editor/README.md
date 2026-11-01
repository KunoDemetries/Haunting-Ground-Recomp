# Haunting Ground world editor

Standalone tool, separate from the game. It reads your own disc image (`DATA.CVM` or the
`.iso`) read-only; no game data is stored here. See `docs/WORLD_EDITOR.md` for the plan,
format evidence and commands.

- `src/` — `hg_world_editor`, the GLFW/OpenGL viewer. Build:
  `cmake --build build --config Release --target hg_world_editor`
- `hgworld/` — Python reference parsers and exporters: `python editor/hgworld --help`
