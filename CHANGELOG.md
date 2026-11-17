# Changelog

## [0.1.0] - 2026-11-17

Fresh TTY-first baseline.

- Add `Environment` snapshot with order/duplicate preservation.
- Add `LaunchContext` for direct launches: uid/gid/groups, cwd, tty and full
  inherited environment.
- Add managed launch contract reserved for the target-only session manager.
- Add `tdesktop --diagnostics`.
- Add unit tests for environment and launch-context behaviour.
- Add design and implementation-plan documents under `dev-docs/`.
