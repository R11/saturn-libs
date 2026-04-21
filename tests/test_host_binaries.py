#!/usr/bin/env python3
"""
Smoke-tests the host-buildable C unit tests under tests/host/.

Builds each binary via `make -C tests/host` and asserts it exits 0.
These tests exercise parts of the C library that don't touch Saturn
hardware -- init validation, matchmaking protocol, etc.

Skipped silently if no `cc`/`clang` toolchain is available.
"""

import os
import shutil
import socket
import struct
import subprocess
import threading

import pytest


HERE = os.path.dirname(os.path.abspath(__file__))
HOST_DIR = os.path.join(HERE, "host")


pytestmark = pytest.mark.skipif(
    shutil.which("cc") is None and shutil.which("clang") is None,
    reason="no C compiler in PATH",
)


def _build_host(targets=None):
    """Build the host-side C tests. Returns path to tests/host."""
    cmd = ["make", "-C", HOST_DIR]
    if targets:
        cmd.extend(targets)
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    return HOST_DIR


def _run(binary: str) -> subprocess.CompletedProcess:
    """Run a compiled host binary from tests/host/."""
    return subprocess.run(
        [os.path.join(HOST_DIR, binary)],
        check=False, capture_output=True, text=True, timeout=30,
    )


class TestInitPlaceholder:
    def test_rejects_placeholder_dial(self):
        _build_host(["test_init_placeholder"])
        result = _run("test_init_placeholder")
        assert result.returncode == 0, \
            "test_init_placeholder failed:\n%s\n%s" % (result.stdout, result.stderr)
        assert "All checks passed." in result.stdout


class TestTransportAndTxBuf:
    def test_transport_override_and_txbuf(self):
        _build_host(["test_transport_and_txbuf"])
        result = _run("test_transport_and_txbuf")
        assert result.returncode == 0, \
            "test_transport_and_txbuf failed:\n%s\n%s" % (result.stdout, result.stderr)
        assert "All checks passed." in result.stdout


class TestConnectAsync:
    def test_nonblocking_connect_via_transport(self):
        _build_host(["test_connect_async"])
        result = _run("test_connect_async")
        assert result.returncode == 0, \
            "test_connect_async failed:\n%s\n%s" % (result.stdout, result.stderr)
        assert "All checks passed." in result.stdout


class TestHeartbeat:
    def test_heartbeat_emission_and_watchdog(self):
        _build_host(["test_heartbeat"])
        result = _run("test_heartbeat")
        assert result.returncode == 0, \
            "test_heartbeat failed:\n%s\n%s" % (result.stdout, result.stderr)
        assert "All checks passed." in result.stdout


# ---------------------------------------------------------------------------
# Matchmaking: mock server + round-trip via saturn_online_matchmake()
# ---------------------------------------------------------------------------

def _read_frame(conn):
    """Read one saturn_online length-prefixed frame off the socket."""
    header = b""
    while len(header) < 2:
        chunk = conn.recv(2 - len(header))
        if not chunk:
            return None
        header += chunk
    (plen,) = struct.unpack(">H", header)
    payload = b""
    while len(payload) < plen:
        chunk = conn.recv(plen - len(payload))
        if not chunk:
            return None
        payload += chunk
    return payload


def _write_frame(conn, payload):
    conn.sendall(struct.pack(">H", len(payload)) + payload)


def _start_mock_matchmaking_server(opponent_id=42, opponent_dial=b"#123#",
                                    extra=b"ABC"):
    """
    Accepts a single TCP client, reads a matchmake request, and writes
    an opponent response.

    Request  : [u16 game_id BE][u8 name_len][name_len bytes]
    Response : [u8 opponent_id][u8 dial_len][dial bytes][u8 extra_len][extra]
    """
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]

    result = {"ok": False, "error": None, "game_id": None, "username": None}
    stop = threading.Event()

    def _run():
        try:
            srv.settimeout(5.0)
            conn, _ = srv.accept()
            conn.settimeout(5.0)
            req = _read_frame(conn)
            if req is None or len(req) < 3:
                result["error"] = "bad request"
                conn.close()
                return
            (game_id,) = struct.unpack(">H", req[:2])
            name_len = req[2]
            username = req[3:3 + name_len].decode("utf-8", errors="replace")
            result["game_id"]  = game_id
            result["username"] = username

            # Build response.
            resp = bytes([opponent_id, len(opponent_dial)]) \
                 + opponent_dial \
                 + bytes([len(extra)]) + extra
            _write_frame(conn, resp)
            # Wait briefly so the client has time to read before we close.
            try:
                conn.settimeout(0.5)
                conn.recv(1)
            except Exception:
                pass
            conn.close()
            result["ok"] = True
        except Exception as e:  # noqa: BLE001
            result["error"] = repr(e)
        finally:
            try: srv.close()
            except OSError: pass
            stop.set()

    t = threading.Thread(target=_run, daemon=True)
    t.start()
    return port, t, result


class TestMatchmaking:
    def test_roundtrip(self):
        _build_host(["test_matchmaking"])
        port, t, server_result = _start_mock_matchmaking_server()
        try:
            result = subprocess.run(
                [os.path.join(HOST_DIR, "test_matchmaking"), str(port)],
                check=False, capture_output=True, text=True, timeout=20,
            )
        finally:
            t.join(timeout=6.0)

        assert result.returncode == 0, \
            "test_matchmaking failed:\n%s\n%s" % (result.stdout, result.stderr)
        assert "All checks passed." in result.stdout
        assert server_result["ok"], server_result
        assert server_result["game_id"]  == 0x1337
        assert server_result["username"] == "saturn-player-1"
