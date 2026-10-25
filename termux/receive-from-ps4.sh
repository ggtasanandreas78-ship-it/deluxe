#!/data/data/com.termux/files/usr/bin/bash
set -Eeuo pipefail
command -v curl >/dev/null || { echo "ثبت curl أولاً: pkg install curl"; exit 1; }
read -r -p "PS4 IP: " IP
read -r -p "FTP port [2121]: " PORT; PORT="${PORT:-2121}"
read -r -p "مسار الملف على PS4 (مثال /data/pkg/example.pkg): " REMOTE
read -r -p "مجلد الحفظ [~/storage/downloads]: " OUT; OUT="${OUT:-$HOME/storage/downloads}"
OUT="${OUT/#\~/$HOME}"; mkdir -p "$OUT"
curl --fail --show-error --ftp-pasv --connect-timeout 10 --retry 2 --progress-bar \
  "ftp://${IP}:${PORT}${REMOTE}" -o "$OUT/$(basename "$REMOTE")"
echo "تم التنزيل إلى $OUT/$(basename "$REMOTE")"
