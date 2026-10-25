# DELUXE PKG MANAGER — PS4 build attempt

## What this package contains
- The approved HTML design preview (`index.html`).
- A native OpenOrbis C bootstrap (`src/main.c`).
- A GitHub Actions workflow that attempts to compile and package a PS4 `.pkg`.
- Termux FTP send/receive scripts.

## Important reality check
This is a **first native build bootstrap**, not the finished app from the reference UI. The C program currently prints a startup message; the complete graphical interface and in-app FTP client still need native implementation. A GitHub Actions run must succeed before a real `.pkg` artifact exists. The workflow is designed to fail rather than silently claim success if no PKG is produced. OpenOrbis toolchain versions and package commands can change; if it fails, use the build diagnostics artifact/log to fix the exact error.

## Build
1. Create a GitHub repository named `DELUXE-PKG-MANAGER`.
2. Upload every file and folder in this archive, including `.github/workflows/build.yml`.
3. Open Actions and run **Build DELUXE PS4 PKG**.
4. If the job succeeds, open that run, go to Artifacts, download `DELUXE-PKG-MANAGER-PKG`, then extract the `.pkg` file.
5. If it fails, open the failed step and share the final error lines; don't download the diagnostics as if they were the app.

## FTP transfer from Termux
Enable GoldHEN FTP Server on the PS4, keep tablet and PS4 on the same LAN, then:
```sh
pkg install curl -y
termux-setup-storage
bash termux/send-to-ps4.sh
```
For receiving a file from the PS4:
```sh
bash termux/receive-from-ps4.sh
```
FTP transfers files only; it does not install them. Installation is done on the PS4 through its supported package installer workflow.

## About the build
The official OpenOrbis toolchain is used because it supports compiling homebrew and packaging it as a PS4 PKG. This archive does not contain a verified prebuilt PKG; only a successful GitHub Actions build can produce one.
