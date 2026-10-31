#!/usr/bin/env python3

from __future__ import annotations

import json
import struct
import sys


def read_message():
    header = sys.stdin.buffer.read(4)
    if len(header) < 4:
        return None
    (length,) = struct.unpack("=I", header)
    payload = sys.stdin.buffer.read(length)
    if len(payload) < length:
        return None
    return json.loads(payload.decode())


def write_message(message):
    payload = json.dumps(message).encode()
    sys.stdout.buffer.write(struct.pack("=I", len(payload)))
    sys.stdout.buffer.write(payload)
    sys.stdout.buffer.flush()


def main():
    origin = sys.argv[1] if len(sys.argv) > 1 else ""
    while True:
        message = read_message()
        if message is None:
            break
        if isinstance(message, dict) and message.get("quit"):
            break
        write_message({"echo": message, "origin": origin})


if __name__ == "__main__":
    main()
