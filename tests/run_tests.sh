#!/bin/bash
# Builds host tests and runs them against the mock FTP server. Usage: tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'kill $SRV 2>/dev/null; rm -rf "$T"' EXIT
mkdir -p "$T/root/games" "$T/root/readonly" "$T/local"
head -c $((5*1024*1024)) /dev/urandom > "$T/root/big.bin"
head -c 300000 /dev/urandom > "$T/root/dropme.bin"
head -c $((3*1024*1024)) /dev/urandom > "$T/local/up_src.bin"
CFLAGS="-Wall -Wextra -Isrc -g -fsanitize=address,undefined"
SRC="src/ftp.c src/config.c src/localfs.c src/platform_host.c src/gfx.c src/ui.c"
${CC:-clang} $CFLAGS -o "$T/test_ftp" tests/test_ftp.c src/ftp.c src/platform_host.c src/platform_common.c || exit 1
python3 tests/mock_ftp.py "$T/root" > "$T/port" & SRV=$!
for i in $(seq 50); do grep -q PORT "$T/port" 2>/dev/null && break; sleep 0.1; done
read -r _ FP SP < "$T/port"
FTP_PORT=$FP SILENT_PORT=$SP LOCAL_DIR="$T/local" "$T/test_ftp"; RC=$?
echo "=== UI tests (scripted buttons against the mock server, ASan+UBSan) ==="
mkdir -p "$T/shots" "$T/uilocal"; cp "$T/local/up_src.bin" "$T/uilocal/"
${CC:-clang} $CFLAGS -o "$T/test_ui" tests/test_ui.c src/ftp.c src/config.c src/localfs.c src/platform_host.c src/platform_common.c src/gfx.c src/ui.c || exit 1
kill $SRV 2>/dev/null; THROTTLE=3000000 python3 tests/mock_ftp.py "$T/root" > "$T/port2" & SRV=$!
for i in $(seq 50); do grep -q PORT "$T/port2" 2>/dev/null && break; sleep 0.1; done
read -r _ FP2 _ < "$T/port2"
DELUXE_CONFIG="$T/ui.ini" DELUXE_LOCAL="$T/uilocal" FTP_PORT=$FP2 LOCAL_DIR="$T/uilocal" SHOT_DIR="$T/shots" "$T/test_ui"; RC2=$?
RC=$((RC|RC2))
if [ -n "${SHOT_OUT:-}" ]; then mkdir -p "$SHOT_OUT"; python3 - "$T/shots" "$SHOT_OUT" <<'PY'
import sys, glob, os
from PIL import Image
for f in sorted(glob.glob(sys.argv[1] + "/*.ppm")):
    Image.open(f).save(os.path.join(sys.argv[2], os.path.basename(f)[:-4] + ".png"))
PY
fi
echo "--- data integrity (sha256) ---"
FAIL=0
cmp_files() { if [ "$(sha256sum < "$1" | cut -d' ' -f1)" = "$(sha256sum < "$2" | cut -d' ' -f1)" ]; then echo "PASS  $3"; else echo "FAIL  $3"; FAIL=1; fi; }
cmp_files "$T/root/big.bin" "$T/local/down_big.bin" "downloaded big.bin is byte-identical"
cmp_files "$T/local/up_src.bin" "$T/root/uploaded.bin" "uploaded file is byte-identical on server"
cmp_files "$T/root/big.bin" "$T/uilocal/big_ok.bin" "UI-driven download is byte-identical"
cmp_files "$T/uilocal/up_src.bin" "$T/root/up_src.bin" "UI-driven upload is byte-identical"
exit $((RC | FAIL))
