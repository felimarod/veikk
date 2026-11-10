# Changelog

## Unreleased

### Fixed
- Chords are re-emitted modifiers first (down) and last (up); `super+a` no
  longer sends a bare `a` first.
- The filters forget held keys and half-read frames when a device goes away.
- A pen error is no longer hidden by opening the pad's keys; each device keeps
  its own problem.
- A second `veikkd` no longer deletes the running one's state (lock file).
- The window no longer reopens an unreadable pen every second, no longer
  destroys a notifier from inside its own signal, and handles EOF and EINTR.
- The virtual tablet copies the pen's real resolution and checks its setup.
- Pad key names the virtual keypad cannot send are rejected up front.
- One minimum area size everywhere.

### Changed
- The service is its own binary, `veikkd`, linked to Qt Core only.
- Monitor changes arrive over Hyprland's event socket and devices over inotify,
  instead of polling `hyprctl` and `/proc`.
- "Is the service running" uses the daemon lock, not a pid that can be reused.
- `veikk status` exits 3 when the service is not running; `--help`/`--version`
  exit 0.
- Service messages are translated by the window.
- PKGBUILD builds in `$srcdir` with the distribution's flags, runs the tests,
  and refreshes udev; `veikk.service` is hardened.

### Added
- Real size (1:1): the pad at its physical size, centred on the monitor.
- Keyboard control and accessible names for the area and the pressure curve.
- Tests for the lock, Hyprland events, layout changes, config repair and the
  Spanish catalog.
