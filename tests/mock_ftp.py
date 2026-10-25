#!/usr/bin/env python3
"""Minimal FTP server for testing the DELUXE FTP client (passive mode only).
Usage: mock_ftp.py ROOT   -> prints 'PORT <ftp> <silent>' then serves until killed.
Special behaviours used by tests:
  user ps4 / pass secret (or anonymous) are accepted, anything else -> 530
  /readonly/*        : STOR refused with 550
  dropme.bin         : RETR sends half the data, then claims success (tests integrity check)
  CWD /killme        : server drops the control connection abruptly
  THROTTLE env       : bytes/sec limit on data connections (to make progress observable)
  silent port        : accepts TCP connections but never speaks (tests timeouts)"""
import os, socket, sys, threading, time, stat

ROOT = os.path.realpath(sys.argv[1])
THROTTLE = int(os.environ.get("THROTTLE", "0"))

def resolve(cwd, p):
    full = os.path.normpath(os.path.join(cwd, p) if not p.startswith("/") else p)
    real = os.path.realpath(os.path.join(ROOT, full.lstrip("/")))
    if not (real == ROOT or real.startswith(ROOT + os.sep)):
        return None, None
    return full, real

def listing(real):
    out = []
    for n in sorted(os.listdir(real)):
        st = os.stat(os.path.join(real, n))
        kind = "d" if stat.S_ISDIR(st.st_mode) else "-"
        out.append("%srwxr-xr-x 1 ps4 ps4 %d Jan 01 12:00 %s\r\n" % (kind, st.st_size, n))
    return "".join(out).encode()

class Session(threading.Thread):
    def __init__(self, c): super().__init__(daemon=True); self.c = c; self.cwd = "/"; self.pasv = None; self.user_ok = False; self.user = ""
    def send(self, s): self.c.sendall((s + "\r\n").encode())
    def data_conn(self):
        self.pasv.settimeout(5)
        d, _ = self.pasv.accept(); self.pasv.close(); self.pasv = None; return d
    def run(self):
        try: self.loop()
        except Exception: pass
        finally:
            try: self.c.close()
            except Exception: pass
    def loop(self):
        self.send("220 mock ftp ready")
        f = self.c.makefile("rb")
        for raw in f:
            line = raw.decode(errors="replace").rstrip("\r\n")
            cmd, _, arg = line.partition(" "); cmd = cmd.upper()
            if cmd == "USER": self.user = arg; self.send("331 password please")
            elif cmd == "PASS":
                if (self.user, arg) in (("ps4", "secret"), ("anonymous", "anonymous")): self.user_ok = True; self.send("230 logged in")
                else: self.send("530 Login incorrect")
            elif cmd == "QUIT": self.send("221 bye"); return
            elif not self.user_ok: self.send("530 Please login")
            elif cmd == "TYPE": self.send("200 Type set")
            elif cmd == "NOOP": self.send("200 ok")
            elif cmd == "PWD": self.send('257 "%s" is current directory' % self.cwd)
            elif cmd == "CWD":
                if arg == "/killme": return
                full, real = resolve(self.cwd, arg)
                if real and os.path.isdir(real): self.cwd = full; self.send("250 ok")
                else: self.send("550 No such directory")
            elif cmd == "MKD":
                full, real = resolve(self.cwd, arg)
                try: os.mkdir(real); self.send('257 "%s" created' % full)
                except Exception: self.send("550 Cannot create directory")
            elif cmd == "SIZE":
                full, real = resolve(self.cwd, arg)
                if real and os.path.isfile(real): self.send("213 %d" % os.path.getsize(real))
                else: self.send("550 No such file")
            elif cmd == "PASV":
                if self.pasv: self.pasv.close()
                self.pasv = socket.socket(); self.pasv.bind(("127.0.0.1", 0)); self.pasv.listen(1)
                p = self.pasv.getsockname()[1]
                self.send("227 Entering Passive Mode (127,0,0,1,%d,%d)" % (p >> 8, p & 255))
            elif cmd == "LIST":
                full, real = resolve(self.cwd, arg.strip() or ".")
                if not real or not os.path.isdir(real): self.send("550 No such directory"); continue
                self.send("150 Here comes the listing")
                d = self.data_conn(); d.sendall(listing(real)); d.close(); self.send("226 Directory send OK")
            elif cmd == "RETR":
                full, real = resolve(self.cwd, arg)
                if not real or not os.path.isfile(real): self.send("550 No such file"); continue
                data = open(real, "rb").read()
                if os.path.basename(real) == "dropme.bin": data = data[: len(data) // 2]
                self.send("150 Opening data connection")
                d = self.data_conn()
                try:
                    for i in range(0, len(data), 32768):
                        d.sendall(data[i:i + 32768])
                        if THROTTLE: time.sleep(32768 / THROTTLE)
                    d.close(); self.send("226 Transfer complete")
                except (BrokenPipeError, ConnectionResetError):
                    self.send("426 Transfer aborted")
            elif cmd == "STOR":
                full, real = resolve(self.cwd, arg)
                if not real or full.startswith("/readonly"): self.send("550 Permission denied"); continue
                self.send("150 Ok to send data")
                d = self.data_conn()
                with open(real, "wb") as out:
                    while True:
                        b = d.recv(65536)
                        if not b: break
                        out.write(b)
                        if THROTTLE: time.sleep(len(b) / THROTTLE)
                d.close(); self.send("226 Transfer complete")
            else: self.send("502 Command not implemented")

def serve(sock, handler):
    while True:
        c, _ = sock.accept(); handler(c)

main = socket.socket(); main.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); main.bind(("127.0.0.1", 0)); main.listen(16)
silent = socket.socket(); silent.bind(("127.0.0.1", 0)); silent.listen(16)
held = []
threading.Thread(target=serve, args=(silent, held.append), daemon=True).start()
print("PORT %d %d" % (main.getsockname()[1], silent.getsockname()[1]), flush=True)
serve(main, lambda c: Session(c).start())
