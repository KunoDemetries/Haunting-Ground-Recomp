# Keyboard controller

Press **F1** in the native game window to open or close the on-screen keyboard
reference. The title bar also shows `F1: Controls`. **Escape closes this panel**;
a separate Escape press with the panel closed exits the game. Holding Escape
while dismissing the panel will not also quit.

Interactive runs start with the host **30 FPS lock enabled**. Press **F2** to
toggle it at any time; the title bar shows `F2: 30 FPS Lock On/Off`. The lock
caps only UI presentation of the existing latest completed frame. Input polling,
guest execution, guest clocks, rendering work and game pixels are unchanged.
Hidden verification and explicit unlimited runs do not enable it.

The reference is a host overlay, not the original pause menu. **The game keeps
running while it is open**, and live controller/keyboard input is released until
it closes. Pause with **Enter** before opening F1 when you need time to read it.

These are the native game's manual-mode defaults. Click the game window to focus it.
All original keyboard bindings are preserved; both analog sticks and all sixteen
DualShock2 button bits are available. A key press supplies full pressure (255)
for pressure-sensitive buttons and releases to zero.

| PS2 control | Keyboard |
| --- | --- |
| D-pad up / right / down / left | I / L / K / J |
| Left stick up / left / down / right | W / A / S / D |
| Right stick up / left / down / right | Arrow keys (Up / Left / Down / Right) |
| Cross | Space |
| Circle | X |
| Square | C |
| Triangle | V |
| L1 / R1 | Q / E |
| L2 / R2 | 1 / 3 (top number row) |
| L3 / R3 (stick clicks) | Left Shift / Right Shift |
| Start / Pause | Enter |
| Select | Tab or Backspace |
| Show / hide controls reference | F1 |
| Toggle host 30 FPS lock | F2 |
| Save replay checkpoint | F5 |
| Close controls reference | Escape (when the panel is open) |
| Close the native window | Escape (when the panel is closed) |

Keyboard sticks use digital endpoints, not variable analog travel. Opposite keys
on an axis cancel to its center; diagonals and both sticks together are supported.
Losing focus releases all inputs. Physical-gamepad buttons combine with keyboard
buttons; a non-centered keyboard axis takes priority, otherwise the existing
physical-gamepad axis/deadzone applies. Physical gamepad support still needs user
validation.

For the user's suggested downward gameplay route, hold **S** for the left stick
or **K** for the D-pad (arrow keys drive the right stick since 2026-10-16). Game-specific movement/actions still depend on
what the original game accepts at the current screen.

Recorded/hidden verification runs intentionally ignore live keyboard input. They
must not be mistaken for an interactive manual window. Keyboard bindings do not
change Project Link's controlled-pulse API, which currently exposes only Left,
Cross and Start. No global keyboard injection is used.

**Memory card.** Both launchers insert a virtual 8 MB memory card in slot 1,
stored at `Documents\Haunting Ground Saves\memory-card-slot1.ps2`. Saves made at
in-game typewriters persist there; a new blank card asks to be formatted the
first time. `RUN_MANUAL_AUTHENTIC_TOC.cmd` has the card from boot, so you can load
a save from the title screen. `RUN_FROM_CHECKPOINT.cmd` inserts it when the
checkpoint replay finishes.

**Checkpoints (F5).** Pressing F5 saves an input-replay checkpoint to
`%TEMP%\haunting-toc-probe\checkpoint.txt`. The title briefly shows "Checkpoint
saved". `RUN_FROM_CHECKPOINT.cmd` replays every saved input from boot at full
speed (no sound while fast-forwarding), then pauses at the checkpoint until you
press a game key. It is not an instant save state; resuming about 2 minutes of
play takes roughly a minute. The previous checkpoint is kept as
`checkpoint.prev.txt`.

The window's video-update count is not independently measured gameplay FPS.
