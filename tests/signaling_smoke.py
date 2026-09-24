#!/usr/bin/env python3
"""Minimal WSS authentication smoke test using only the Python standard library."""

import argparse
import base64
import hashlib
import json
import os
import socket
import ssl
import struct


def connect(host: str, port: int):
    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    raw = socket.create_connection((host, port), timeout=5)
    stream = context.wrap_socket(raw, server_hostname=host)
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        f"GET /ws HTTP/1.1\r\nHost: {host}:{port}\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    )
    stream.sendall(request.encode())
    response = b""
    while b"\r\n\r\n" not in response:
        response += stream.recv(4096)
    assert response.startswith(b"HTTP/1.1 101"), response
    expected = base64.b64encode(
        hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
    )
    assert b"sec-websocket-accept: " + expected.lower() in response.lower(), response
    return stream


def send_text(stream, payload: dict):
    data = json.dumps(payload, separators=(",", ":")).encode()
    mask = os.urandom(4)
    header = bytearray([0x81])
    if len(data) < 126:
        header.append(0x80 | len(data))
    else:
        header.append(0x80 | 126)
        header.extend(struct.pack("!H", len(data)))
    header.extend(mask)
    header.extend(byte ^ mask[index % 4] for index, byte in enumerate(data))
    stream.sendall(header)


def receive_text(stream) -> dict:
    first, second = stream.recv(2)
    opcode = first & 0x0F
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", stream.recv(2))[0]
    elif length == 127:
        length = struct.unpack("!Q", stream.recv(8))[0]
    assert opcode == 1, f"unexpected websocket opcode {opcode}"
    payload = b""
    while len(payload) < length:
        payload += stream.recv(length - len(payload))
    return json.loads(payload)


def authenticate(host: str, port: int, token: str) -> dict:
    stream = connect(host, port)
    try:
        send_text(stream, {"type": "authenticate", "token": token})
        return receive_text(stream)
    finally:
        stream.close()


def assert_busy(host: str, port: int, token: str, target: str):
    stream = connect(host, port)
    try:
        send_text(stream, {"type": "authenticate", "token": token})
        authenticated = receive_text(stream)
        assert authenticated.get("type") == "authenticated", authenticated
        send_text(stream, {"type": "start", "target": target})
        response = receive_text(stream)
        assert response == {"type": "error", "message": "target-user-busy"}, response
    finally:
        stream.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", default=18080, type=int)
    token_source = parser.add_mutually_exclusive_group(required=True)
    token_source.add_argument("--token")
    token_source.add_argument("--token-file")
    parser.add_argument("--expect-busy-target")
    args = parser.parse_args()
    token = args.token
    if args.token_file:
        with open(args.token_file, encoding="utf-8") as stream:
            token = stream.read().rstrip("\r\n")

    rejected = authenticate(args.host, args.port, "definitely-wrong")
    accepted = authenticate(args.host, args.port, token)
    assert rejected.get("type") == "auth-error", rejected
    assert accepted.get("type") == "authenticated", accepted
    if args.expect_busy_target:
        assert_busy(args.host, args.port, token, args.expect_busy_target)
    print("WSS authentication smoke test passed")


if __name__ == "__main__":
    main()
