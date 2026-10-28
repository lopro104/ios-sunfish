#!/usr/bin/env python3
"""Server for userland/net/sweet-bw.c: port 5201 sends zeros to whoever connects (phone rx test), port 5202 sinks (phone tx test)."""
import socket, threading
def serve(port, send):
    srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port)); srv.listen(4)
    while True:
        c, _ = srv.accept()
        def run(c=c):
            try:
                buf = b"x" * 65536
                while True:
                    if send: c.sendall(buf)
                    elif not c.recv(65536): break
            except OSError: pass
            c.close()
        threading.Thread(target=run, daemon=True).start()
threading.Thread(target=serve, args=(5201, True), daemon=True).start()
serve(5202, False)
