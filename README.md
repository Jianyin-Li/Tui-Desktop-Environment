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
- `--diagnostics` reports launch identity and environment metadata without
  printing secrets.

Design and implementation planning live in:

- `dev-docs/design-v1-fullscreen.md` — TTY design, key bindings, systemd units
- `dev-docs/implementation-plan.md` — CLI/desktop experience and roadmap

## Build

On a memory-constrained machine, build in tmpfs and limit parallelism:

```sh
SRC=~/Projects/Tui-Desktop-Environment
BUILD=/tmp/tdesktop-build

cmake -B "$BUILD" -S "$SRC" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j2
ctest --test-dir "$BUILD" --output-on-failure

"$BUILD/tdesktop" --diagnostics
```

## Usage

```sh
tdesktop --version
tdesktop --diagnostics
tdesktop --help
```

## License

LGPL-3.0. See `LICENSE`.
