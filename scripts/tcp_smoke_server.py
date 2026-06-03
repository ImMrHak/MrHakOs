#!/usr/bin/env python3
import socket
import sys

out_path = sys.argv[1] if len(sys.argv) > 1 else "bin/tcp-received.log"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", port))
    server.listen(1)
    server.settimeout(45)
    conn, addr = server.accept()
    with conn:
        conn.settimeout(5)
        data = conn.recv(1024)

with open(out_path, "wb") as f:
    f.write(data)
    f.write(b"\n")
