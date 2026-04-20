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
import subprocess

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
