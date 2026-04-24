#!/usr/bin/env python3
"""
echo_server.py - Minimal TCP echo for round-trip testing the hello example.

Accepts a single client on a TCP port and echoes every byte it receives
back to the client.  Combined with `bridge.py --mock`, this is enough
to verify end-to-end byte flow from a Saturn (or simulated client) all
the way out and back:

    Saturn/client -> bridge (--mock) -> echo_server -> bridge -> Saturn/client

Usage:
    python3 tools/echo_server.py                    # listen on 0.0.0.0:4821
    python3 tools/echo_server.py --port 9000
    python3 tools/echo_server.py --host 127.0.0.1 --port 4821

The server is frame-agnostic: it echoes raw bytes, so any framing used
by the caller (including the saturn-io [LEN_HI][LEN_LO][payload]
format) round-trips intact.
"""

import argparse
import logging
import socket
import sys

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("echo_server")


def serve(host: str, port: int) -> None:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(4)
    log.info("Echo server listening on %s:%d", host, port)

    try:
        while True:
            conn, addr = srv.accept()
            log.info("Client connected: %s:%d", addr[0], addr[1])
            total = 0
            try:
                while True:
                    data = conn.recv(4096)
                    if not data:
                        break
                    conn.sendall(data)
                    total += len(data)
            except OSError as e:
                log.warning("Client error: %s", e)
            finally:
                log.info("Client %s:%d disconnected (%d bytes echoed)",
                         addr[0], addr[1], total)
                try:
                    conn.close()
                except OSError:
                    pass
    except KeyboardInterrupt:
        log.info("Echo server shutting down.")
    finally:
        try:
            srv.close()
        except OSError:
            pass


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Minimal TCP echo server for saturn-io round-trip "
                    "testing.")
    parser.add_argument("--host", default="0.0.0.0",
                        help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=4821,
                        help="Port to listen on (default: 4821)")
    args = parser.parse_args()

    try:
        serve(args.host, args.port)
    except OSError as e:
        log.error("Failed to bind %s:%d: %s", args.host, args.port, e)
        sys.exit(1)


if __name__ == "__main__":
    main()
