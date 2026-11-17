# TDESKTOP

A TTY-first desktop environment for Linux virtual consoles, built around
FTXUI, libvterm and a PTY-per-pane architecture.

The project is currently at **0.1.0**, starting with the launch environment.
The TTY UI and session manager are not implemented yet.

## Status

- Direct launch context: inherits the invoking shell's uid/gid, groups, cwd,
  umask and full environment; no PAM session and no manager.
- Managed launch contract: reserved for `tdesktop-manager` started by the
  systemd target.
- TTY UI (0.1.0): desktop-style Home with app tiles and a running-app card;
  applications open in a full-screen pane rendered through libvterm with ANSI
  colors/attributes, cursor handling, alternate screen and resize support.
- Launch starts on the desktop Home. `Enter` opens the Terminal tile.
- `Alt+Shift+Q` closes the current TUI application and returns to Home.
- `Alt+Shift+X` exits the TDESKTOP TUI program only; it never signals the
  target-only session manager.
- `--diagnostics` reports launch identity and environment metadata without
  printing secrets.

The UI deliberately parses terminal output instead of printing raw escape
sequences as text. Memory is proportional to the visible screen: there is no
scrollback buffer in 0.1.0. Measured RSS was about 5–6 MB for the UI process
with one shell at 100×30; the shell and its children are separate processes.

Design and implementation planning live in:

- `dev-docs/design-v1-fullscreen.md` — TTY design, key bindings, systemd units
- `dev-docs/implementation-plan.md` — CLI/desktop experience and roadmap

## Dependencies

```sh
sudo apt install libftxui-dev libvterm-dev
```

## Build

On a memory-constrained machine, build in tmpfs and limit parallelism:

```sh
SRC=~/Projects/Tui-Desktop-Environment
BUILD=/tmp/tdesktop-build

cmake -B "$BUILD" -S "$SRC" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j2
ctest --test-dir "$BUILD" --output-on-failure
```

If libvterm is unpacked somewhere instead of installed system-wide:

```sh
cmake -B "$BUILD" -S "$SRC" \
  -DVTERM_INCLUDE_DIR=/path/to/include \
  -DVTERM_LIBRARY=/path/to/libvterm.so
```

## Usage

```sh
# TTY UI with an interactive shell
tdesktop

# Environment diagnostics (works without a TTY)
tdesktop --diagnostics

tdesktop --version
tdesktop --help
```

UI keys:

- `Enter` on the Terminal tile — open/resume the shell
- `Alt+Shift+H` — hide/show the current application (Home)
- `Alt+Shift+Q` — close the current application, return to Home
- `Alt+Shift+X` — exit TDESKTOP (the TUI program); does not touch the manager
- `←/→` — select a Home tile

## License

LGPL-3.0. See `LICENSE`.
