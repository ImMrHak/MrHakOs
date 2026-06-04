#!/usr/bin/env python3
"""Minimal SOCKS5 proxy for testing the MrHakOS `socks5` terminal command.

It implements just enough of RFC 1928 (and RFC 1929 username/password auth) to
exercise the kernel's SOCKS5 client: the no-auth and user/pass methods, the
CONNECT command, and IPv4 (ATYP 0x01) and domain-name (ATYP 0x03) targets.
Each accepted connection is handled in its own thread and relayed to the target.

Usage:
    python3 scripts/socks5_proxy.py                 # no auth, listen on 0.0.0.0:1080
    python3 scripts/socks5_proxy.py --port 9050
    python3 scripts/socks5_proxy.py --user alice --password secret

From inside MrHakOS (QEMU user networking reaches the host gateway at 10.0.2.2):
    socks5 10.0.2.2 1080 example.com 80 http
    socks5 -u alice:secret 10.0.2.2 1080 1.1.1.1 80 http
"""
import argparse
import socket
import struct
import sys
import threading

VER = 0x05
AUTH_NONE = 0x00
AUTH_USERPASS = 0x02
AUTH_NONE_ACCEPTABLE = 0xFF
CMD_CONNECT = 0x01
ATYP_IPV4 = 0x01
ATYP_DOMAIN = 0x03
ATYP_IPV6 = 0x04
REP_SUCCESS = 0x00
REP_GENERAL_FAILURE = 0x01
REP_HOST_UNREACHABLE = 0x04
REP_CONN_REFUSED = 0x05
REP_CMD_NOT_SUPPORTED = 0x07
REP_ATYP_NOT_SUPPORTED = 0x08


def recvn(sock, n):
    """Read exactly n bytes or raise if the peer closes early."""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("peer closed during read")
        buf += chunk
    return buf


def relay(src, dst):
    try:
        while True:
            data = src.recv(4096)
            if not data:
                break
            dst.sendall(data)
    except OSError:
        pass
    finally:
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def send_reply(conn, rep, bind_host="0.0.0.0", bind_port=0):
    try:
        addr = socket.inet_aton(bind_host)
    except OSError:
        addr = b"\x00\x00\x00\x00"
    conn.sendall(struct.pack("!BBBB", VER, rep, 0x00, ATYP_IPV4) + addr + struct.pack("!H", bind_port))


def negotiate_auth(conn, peer, want_user, want_pass):
    ver, nmethods = recvn(conn, 2)
    if ver != VER:
        raise ConnectionError(f"bad greeting version {ver}")
    methods = recvn(conn, nmethods)
    require_auth = want_user is not None
    if require_auth:
        if AUTH_USERPASS not in methods:
            conn.sendall(struct.pack("!BB", VER, AUTH_NONE_ACCEPTABLE))
            raise ConnectionError("client did not offer username/password")
        conn.sendall(struct.pack("!BB", VER, AUTH_USERPASS))
        aver = recvn(conn, 1)[0]
        ulen = recvn(conn, 1)[0]
        uname = recvn(conn, ulen).decode(errors="replace")
        plen = recvn(conn, 1)[0]
        passwd = recvn(conn, plen).decode(errors="replace")
        ok = (aver == 0x01 and uname == want_user and passwd == want_pass)
        conn.sendall(struct.pack("!BB", 0x01, 0x00 if ok else 0x01))
        if not ok:
            raise ConnectionError(f"auth rejected for user {uname!r}")
        print(f"[socks5] {peer} authenticated as {uname!r}")
    else:
        if AUTH_NONE not in methods:
            conn.sendall(struct.pack("!BB", VER, AUTH_NONE_ACCEPTABLE))
            raise ConnectionError("client did not offer no-auth")
        conn.sendall(struct.pack("!BB", VER, AUTH_NONE))


def read_request(conn):
    ver, cmd, rsv, atyp = recvn(conn, 4)
    if ver != VER:
        raise ConnectionError(f"bad request version {ver}")
    if atyp == ATYP_IPV4:
        host = socket.inet_ntoa(recvn(conn, 4))
    elif atyp == ATYP_DOMAIN:
        dlen = recvn(conn, 1)[0]
        host = recvn(conn, dlen).decode(errors="replace")
    elif atyp == ATYP_IPV6:
        host = socket.inet_ntop(socket.AF_INET6, recvn(conn, 16))
    else:
        send_reply(conn, REP_ATYP_NOT_SUPPORTED)
        raise ConnectionError(f"unsupported ATYP {atyp}")
    port = struct.unpack("!H", recvn(conn, 2))[0]
    return cmd, host, port


def handle(conn, peer, want_user, want_pass):
    target = None
    try:
        negotiate_auth(conn, peer, want_user, want_pass)
        cmd, host, port = read_request(conn)
        if cmd != CMD_CONNECT:
            send_reply(conn, REP_CMD_NOT_SUPPORTED)
            print(f"[socks5] {peer} unsupported CMD {cmd}")
            return
        print(f"[socks5] {peer} CONNECT {host}:{port}")
        try:
            target = socket.create_connection((host, port), timeout=10)
        except socket.gaierror:
            send_reply(conn, REP_HOST_UNREACHABLE)
            print(f"[socks5] {peer} resolve failed for {host}")
            return
        except ConnectionRefusedError:
            send_reply(conn, REP_CONN_REFUSED)
            print(f"[socks5] {peer} connection refused by {host}:{port}")
            return
        except OSError as exc:
            send_reply(conn, REP_GENERAL_FAILURE)
            print(f"[socks5] {peer} connect error: {exc}")
            return
        bind_host, bind_port = target.getsockname()[:2]
        send_reply(conn, REP_SUCCESS, bind_host, bind_port)
        print(f"[socks5] {peer} tunnel up via {bind_host}:{bind_port}")
        t = threading.Thread(target=relay, args=(target, conn), daemon=True)
        t.start()
        relay(conn, target)
        t.join()
    except (ConnectionError, OSError) as exc:
        print(f"[socks5] {peer} closed: {exc}")
    finally:
        for s in (conn, target):
            if s is not None:
                try:
                    s.close()
                except OSError:
                    pass


def main():
    ap = argparse.ArgumentParser(description="Minimal SOCKS5 proxy for MrHakOS tests")
    ap.add_argument("--host", default="0.0.0.0", help="bind address (default 0.0.0.0)")
    ap.add_argument("--port", type=int, default=1080, help="listen port (default 1080)")
    ap.add_argument("--user", default=None, help="require this username (enables RFC 1929 auth)")
    ap.add_argument("--password", default="", help="required password when --user is set")
    args = ap.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(16)
        auth = "username/password" if args.user else "no-auth"
        print(f"[socks5] listening on {args.host}:{args.port} ({auth})")
        try:
            while True:
                conn, addr = server.accept()
                peer = f"{addr[0]}:{addr[1]}"
                threading.Thread(
                    target=handle,
                    args=(conn, peer, args.user, args.password),
                    daemon=True,
                ).start()
        except KeyboardInterrupt:
            print("\n[socks5] shutting down")
            return 0


if __name__ == "__main__":
    sys.exit(main())
