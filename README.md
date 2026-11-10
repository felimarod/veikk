![veikk showing a pen stroke on the test canvas](docs/screenshot.png)

# veikk

Set up a Veikk drawing tablet on Omarchy: which part of the pad is used, which
monitor it drives, how pressure feels, and what the pad buttons send.

Built and tested against the VK640 (USB `2feb:0005`). Other Veikk pads use the
same standard HID reports and may work, but have not been tried.

## Why it exists

Hyprland does not register the VK640's pen as a tablet, only its mouse and
keyboard interfaces, so none of Hyprland's `input:tablet:*` options reach it.
`veikk daemon` grabs the real pen and pad keys and re-emits them through a
virtual tablet (`Veikk Virtual Tablet`) and keypad that Hyprland does
recognise. Everything is done in software through evdev and uinput: nothing is
ever written to the tablet itself.

## Use

```sh
veikk           # the window
veikk daemon    # the service the window starts and stops
veikk status    # add --json for scripts
```

Press **Start service** in the window; it runs `systemctl --user enable --now
veikk.service`. Settings live in `~/.config/veikk/config.json` and the daemon
applies every change as soon as it is saved.

The official `vktablet` driver also takes the tablet over, so do not run both.

### Hyprland's own options

Once the service runs, Hyprland lists `veikk-virtual-tablet` under tablets, so
its options work too, for example in `~/.config/hypr/input.lua`:

```lua
hl.device({ name = "veikk-virtual-tablet", output = "DP-1" })
```

veikk already spreads the pen over the monitor you pick, so you only need that
if you prefer Hyprland to own the monitor choice. Leave the window's monitor on
"All monitors" in that case.

## Shortcuts

| Key | Action |
| --- | --- |
| `1` – `4` | Switch page |
| `Space` | Clear the test canvas |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo a setting |
| `?` | Show the shortcuts |
| `Q` | Quit |

## Build

```sh
bin/build      # build/veikk
bin/test       # unit tests, offscreen
bin/install    # makepkg -fsi, so it shows up in the launcher
```

Needs `qt6-base`, `qt6-declarative` and `qt6-wayland`.

## Limits

- Pressure, area and buttons are applied by the service, so they stop applying
  if it stops, and the pad goes back to behaving as the desktop saw it before.
- The tablet's own on-board settings (the proprietary `hidraw` channel the
  official driver uses) are not touched.
- A pad button is remapped by the keys it types. A chord that two buttons both
  type cannot be told apart.
