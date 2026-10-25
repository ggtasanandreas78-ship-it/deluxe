#!/data/data/com.termux/files/usr/bin/bash
set -Eeuo pipefail
command -v curl >/dev/null || { echo "ثبت curl أولاً: pkg install curl"; exit 1; }
read -r -p "PS4 IP: " IP
read -r -p "FTP port [2121]: " PORT; PORT="${PORT:-2121}"
read -r -p "مسار ملف PKG على التابلت: " FILE
FILE="${FILE/#\~/$HOME}"
[[ -f "$FILE" ]] || { echo "الملف غير موجود"; exit 1; }
read -r -p "مجلد الوجهة على PS4 [/data/pkg]: " DIR; DIR="${DIR:-/data/pkg}"
echo "إرسال $(basename "$FILE") إلى $IP:$PORT$DIR/"
curl --fail --show-error --ftp-pasv --connect-timeout 10 --retry 2 --progress-bar \
  -T "$FILE" "ftp://${IP}:${PORT}${DIR%/}/$(basename "$FILE")"
echo "اكتمل النقل."
