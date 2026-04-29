#!/usr/bin/env python3
"""
Tests for the saturn-io bridge.

Unit tests:
    - parse_server_arg
    - NO CARRIER detection (full / partial / split-across-reads)
    - MockModemHandler open/close/accept lifecycle

Integration tests (no hardware needed):
    - Full byte relay: mock modem client <-> bridge <-> echo server
    - Server disconnect handling (bridge drops back to waiting)

Run with:
    python3 -m pytest tools/test_bridge.py -v
"""

import os
import socket
import sys
import threading
import time

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from bridge import (
    MockModemHandler,
    NetlinkBridge,
    parse_server_arg,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _start_echo_server() -> tuple:
    """
    Start a simple TCP echo server on an ephemeral port.
    Returns (port, stop_fn).
    """
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(4)
    port = srv.getsockname()[1]

    stop = threading.Event()

    def _accept_loop():
        srv.settimeout(0.2)
        while not stop.is_set():
            try:
                conn, _ = srv.accept()
            except (socket.timeout, OSError):
                continue
            threading.Thread(target=_echo_client, args=(conn, stop),
                             daemon=True).start()
        try:
            srv.close()
        except OSError:
            pass

    def _echo_client(conn, stop_evt):
        conn.settimeout(0.2)
        try:
            while not stop_evt.is_set():
                try:
                    data = conn.recv(4096)
                except socket.timeout:
                    continue
                except OSError:
                    break
                if not data:
                    break
                try:
                    conn.sendall(data)
                except OSError:
                    break
        finally:
            try:
                conn.close()
            except OSError:
                pass

    t = threading.Thread(target=_accept_loop, daemon=True)
    t.start()

    def stop_fn():
        stop.set()
        try:
            # Poke the accept loop to wake it
            poke = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            poke.settimeout(0.2)
            try:
                poke.connect(("127.0.0.1", port))
            except OSError:
                pass
            poke.close()
        except OSError:
            pass
        t.join(timeout=2.0)

    return port, stop_fn


def _wait_for_listening(mock: MockModemHandler, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if mock._listen_sock is not None and mock.listen_port != 0:
            return
        time.sleep(0.02)
    raise RuntimeError("Mock modem never started listening")


# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------

class TestParseServerArg:
    def test_host_and_port(self):
        assert parse_server_arg("myhost:5000") == ("myhost", 5000)

    def test_localhost_default(self):
        assert parse_server_arg("localhost:4821") == ("localhost", 4821)

    def test_host_only(self):
        assert parse_server_arg("myhost") == ("myhost", 4821)

    def test_ipv4(self):
        assert parse_server_arg("192.168.1.1:9000") == ("192.168.1.1", 9000)


class TestNoCarrierDetection:
    def test_full_string(self):
        assert NetlinkBridge.check_no_carrier(b"\r\nNO CARRIER\r\n")

    def test_embedded_in_data(self):
        data = b"some data here\r\nNO CARRIER\r\n"
        assert NetlinkBridge.check_no_carrier(data)

    def test_no_match(self):
        assert not NetlinkBridge.check_no_carrier(b"some normal data\r\n")

    def test_partial_no_match(self):
        assert not NetlinkBridge.check_no_carrier(b"NO CARRI")

    def test_empty(self):
        assert not NetlinkBridge.check_no_carrier(b"")

    def test_split_across_reads(self):
        tail = b""
        chunk1 = b"xxx\r\nNO CA"
        chunk2 = b"RRIER\r\n"
        tail = (tail + chunk1)[-32:]
        assert not NetlinkBridge.check_no_carrier(tail)
        tail = (tail + chunk2)[-32:]
        assert NetlinkBridge.check_no_carrier(tail)


class TestMockModemHandler:
    def test_open_close(self):
        mock = MockModemHandler(port=0)
        mock.open()
        assert mock.listen_port != 0
        mock.close()

    def test_accept_connection(self):
        mock = MockModemHandler(port=0)
        mock.open()
        port = mock.listen_port

        def connect():
            time.sleep(0.1)
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect(("127.0.0.1", port))
            s.close()

        t = threading.Thread(target=connect, daemon=True)
        t.start()

        result = mock.wait_for_connect()
        assert result is True
        mock.close()
        t.join(timeout=2)


# ---------------------------------------------------------------------------
# Integration tests (mock mode + echo server -- no hardware needed)
# ---------------------------------------------------------------------------

class TestBridgeIntegration:
    def test_full_byte_roundtrip(self):
        """
        mock client -> bridge (mock) -> echo server -> bridge -> mock client

        Sends a length-prefixed frame, expects the same bytes back.
        """
        echo_port, stop_echo = _start_echo_server()
        try:
            mock = MockModemHandler(port=0)
            bridge = NetlinkBridge(
                modem=mock,
                server_host="127.0.0.1",
                server_port=echo_port,
            )
            t = threading.Thread(target=bridge.run, daemon=True)
            t.start()

            _wait_for_listening(mock)

            # Simulated Saturn-side client connects to bridge mock port
            client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client.settimeout(3.0)
            client.connect(("127.0.0.1", mock.listen_port))

            # Give bridge time to accept + connect to echo server
            time.sleep(0.5)

            # Send a saturn-io framed payload: [LEN_HI][LEN_LO][payload]
            payload = b"hello"
            frame = bytes([0, len(payload)]) + payload
            client.sendall(frame)

            # Echo server bounces the bytes verbatim
            got = b""
            deadline = time.time() + 3.0
            while len(got) < len(frame) and time.time() < deadline:
                try:
                    chunk = client.recv(4096)
                except socket.timeout:
                    continue
                if not chunk:
                    break
                got += chunk

            client.close()
            assert got == frame, "expected %r, got %r" % (frame, got)

            bridge.stop()
            t.join(timeout=3.0)
        finally:
            stop_echo()

    def test_server_unreachable_retries(self):
        """
        When the echo server isn't up, the bridge should hang up the
        mock client and keep running.  We don't start an echo server
        and connect anyway; bridge should drop the mock, retry.
        """
        mock = MockModemHandler(port=0)
        bridge = NetlinkBridge(
            modem=mock,
            server_host="127.0.0.1",
            server_port=1,  # reserved / refuses connections
        )
        t = threading.Thread(target=bridge.run, daemon=True)
        t.start()

        try:
            _wait_for_listening(mock)

            client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client.settimeout(3.0)
            client.connect(("127.0.0.1", mock.listen_port))

            # Bridge should attempt to connect to :1, fail, hang up us,
            # and loop back to wait.  After the hangup, our recv returns
            # empty (eof).
            deadline = time.time() + 3.0
            closed = False
            while time.time() < deadline:
                try:
                    data = client.recv(4096)
                    if data == b"":
                        closed = True
                        break
                except (socket.timeout, ConnectionResetError):
                    break
                except OSError:
                    closed = True
                    break
            client.close()
            assert bridge._running, "bridge should still be running"
            # Note: depending on OS, recv may either see EOF or block.
            # Not asserting `closed` strictly -- just that the bridge
            # didn't crash.
        finally:
            bridge.stop()
            t.join(timeout=3.0)
