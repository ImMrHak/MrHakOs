import socket

HOST = "0.0.0.0"
PORT = 9001

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))

print(f"[udp-server] Listening on UDP {PORT}...")

while True:
    data, addr = sock.recvfrom(2048)
    print(f"[udp-server] From {addr}: {data.decode(errors='replace')}")
