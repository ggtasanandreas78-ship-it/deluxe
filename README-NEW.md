# DELUXE PKG MANAGER (PS4 homebrew, OpenOrbis)

Native PS4 app with a neon-blue GUI (software-rendered to the console framebuffer, controller driven)
and a real FTP client (passive mode): browse the console's folders, upload/download files with live
progress, speed and ETA, and see real connection status.

## Status - read this first
| Part | State |
|---|---|
| FTP client (`src/ftp.c`) | **Tested on host** against a mock FTP server: login/auth errors, timeouts, refused connections, LIST, download, upload, byte-identical checks (sha256), cancel, truncated-transfer detection, server drop. 37 checks, ASan+UBSan clean. |
| UI / settings / config / browser (`ui.c`, `gfx.c`, `config.c`) | **Tested on host** by scripted button presses (connect, browse, transfer, overwrite prompt, cancel with O, settings validation + persistence). Screenshots in `docs/`. |
| PS4 layer (`src/platform_ps4.c`): video, pad, sceNet, toast notifications | **Written, never compiled or run.** First CI run will likely show header/signature mismatches to fix. |
| PKG build (`Makefile`, `.github/workflows/build.yml`) | **Untested.** Never run in Actions. |
| On a real PS4 | **Nothing tested.** |

## Controls
Home: D-pad + X. Browser: L1/R1 switch pane, X open dir / transfer file, O cancel running transfer or go back,
Triangle refresh, Square create remote folder. Settings: X edit text (D-pad up/down changes the character,
left/right moves, Square deletes), Triangle saves, O discards. Config file: `/data/deluxe/config.ini`.

## Limits / design decisions
* IPv4 address only (no DNS). FTP only (no FTPS/SFTP), passive mode, unix-style `LIST` output.
* UI text is English (bitmap font, ASCII only). HTML is **not** rendered on the console; `index.html` is
  only the visual reference for the colour theme.
* Existing files are overwritten only after a second X press. No resume support.
* Local/remote permissions: local folders are probed for read/write (shown as [RW]/[RO]); remote permission
  problems are reported from the server's reply (e.g. `550 Permission denied`). Folders outside `/data`
  (e.g. `/user`, `/mnt/usb0`) depend on the sandbox rights of your jailbreak/launcher - untested.

## Build
CI: push -> `host-tests` then `build-pkg` -> artifact `DELUXE-PKG-MANAGER-pkg`.
Local: `make test` (host tests, needs gcc+python3) / `make` (needs `OO_PS4_TOOLCHAIN`, clang, lld).
