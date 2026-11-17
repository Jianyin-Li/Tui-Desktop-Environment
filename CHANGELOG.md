# Changelog

## [0.1.0] - 2026-11-17

Fresh TTY-first baseline.

- Add `Environment` snapshot with order/duplicate preservation.
- Add `LaunchContext` for direct launches: uid/gid/groups, cwd, tty and full
  inherited environment.
- Add managed launch contract reserved for the target-only session manager.
- Add `tdesktop --diagnostics`.
- Add a full-screen TTY shell UI rendered through FTXUI and libvterm.
  - Desktop-style Home with Terminal/Exit tiles and a running-app card.
  - Home uses the terminal's native background (no wallpaper fill); only
    borders, text and the selected tile use accent colors.
  - `Enter` opens the Terminal tile; `Alt+Shift+H` hides/resumes.
  - `Alt+Shift+Q` closes the current application and returns to Home.
  - `Alt+Shift+X` exits only the TDESKTOP TUI program, not a manager.
  - Parses ANSI/OSC output (colors, attrs, cursor, alternate screen), never
    prints escape sequences as text.
  - Keeps memory proportional to the visible screen (no scrollback in 0.1.0).
- Add ANSI/SGR regression test proving escape bytes are consumed by libvterm
  and do not appear as visible cells.
- Add unit tests for environment and launch-context behaviour.
- Add design and implementation-plan documents under `dev-docs/`.
