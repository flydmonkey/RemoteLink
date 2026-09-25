import socket
import struct


def receive_exact(connection, size):
    data = b""
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise ConnectionError("client disconnected")
        data += chunk
    return data


def serve_raw_echo(connection):
    connection.sendall(b"RFB 003.008\n")
    connection.sendall(receive_exact(connection, 4))


def serve_rfb_handshake(connection):
    connection.sendall(b"RFB 003.008\n")
    receive_exact(connection, 12)
    connection.sendall(b"\x01\x01")  # One security type: None
    receive_exact(connection, 1)
    connection.sendall(struct.pack("!I", 0))
    receive_exact(connection, 1)  # ClientInit shared flag
    pixel_format = struct.pack("!BBBBHHHBBB3x", 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
    name = b"RemoteLink integration VNC"
    connection.sendall(struct.pack("!HH", 2, 2) + pixel_format + struct.pack("!I", len(name)) + name)
    connection.settimeout(5)
    try:
        while connection.recv(65536):
            pass
    except (socket.timeout, ConnectionError):
        pass


with socket.socket() as server:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", 5909))
    server.listen(2)
    print("ready", flush=True)
    first, _ = server.accept()
    with first:
        serve_raw_echo(first)
    second, _ = server.accept()
    with second:
        serve_rfb_handshake(second)

