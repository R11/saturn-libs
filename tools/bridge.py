#!/usr/bin/env python3
"""
saturn-io NetLink Modem-to-TCP Bridge

Answers incoming modem calls from the Sega Saturn NetLink and relays
bytes bidirectionally to/from a TCP server.  The bridge is transparent
-- it does not parse or modify the data stream.  Any game-specific
protocol (SCP, chat, game state, whatever) rides on top unchanged.

Architecture:

    Saturn NetLink ---phone cable---> USB modem ---serial---> bridge ---TCP---> your server
                        OR
    test client  ------------------------------TCP-----> bridge (mock mode)

Modes:
    --mock          Listen for TCP on 127.0.0.1:2337 instead of a serial
                    modem.  Good for development without hardware.
    --direct        Send ATA immediately without waiting for RING.  Use
                    when there's no phone exchange providing ring voltage
                    (e.g. a modem-to-modem cable).
    --manual-answer Wait for RING then send ATA explicitly (instead of
                    the default ATS0=1 auto-answer).
    --server HOST:PORT   Where to relay bytes.  Default localhost:4821.

Usage:
    python3 tools/bridge.py --mock
    python3 tools/bridge.py --serial-port /dev/cu.usbmodem1101
    python3 tools/bridge.py --mock --server 127.0.0.1:9000
    python3 tools/bridge.py --list-ports

The bridge has no knowledge of the payload it relays.  Ship it alongside
whatever PC-side server your game needs; the bridge just pushes bytes.
"""

import argparse
import logging
import select
import socket
import sys
import time

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("saturn_io_bridge")

DEFAULT_BAUD = 9600
DEFAULT_SERVER = "localhost:4821"
DEFAULT_MOCK_PORT = 2337

# How many bytes to check at the tail of serial input for NO CARRIER.
NO_CARRIER_WINDOW = 32


# ---------------------------------------------------------------------------
# Modem handler interface + implementations
# ---------------------------------------------------------------------------

class ModemHandler:
    """Real serial modem via pyserial."""

    def __init__(self, port: str, baud: int = DEFAULT_BAUD,
                 manual_answer: bool = False, direct: bool = False):
        self.port = port
        self.baud = baud
        self.manual_answer = manual_answer
        self.direct = direct
        self._serial = None
        self._leftover = b""  # Data read during CONNECT that belongs to relay

    def open(self) -> None:
        import serial
        self._serial = serial.Serial(
            port=self.port,
            baudrate=self.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0,
            rtscts=False,
            xonxoff=False,
        )
        log.info("Opened serial port %s at %d baud", self.port, self.baud)

    def close(self) -> None:
        if self._serial:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None

    def read(self, size: int = 4096) -> bytes:
        if self._leftover:
            data = self._leftover
            self._leftover = b""
            return data
        n = self._serial.in_waiting
        if n:
            return self._serial.read(min(n, size))
        return b""

    def write(self, data: bytes) -> None:
        self._serial.write(data)
        self._serial.flush()

    def hangup(self) -> None:
        """Hang up the modem by escaping to command mode and sending ATH."""
        log.info("Hanging up modem...")
        # Guard time silence (S12 register, typically 1s)
        time.sleep(1.1)
        self._serial.write(b"+++")
        self._serial.flush()
        time.sleep(1.1)
        # Drain OK response from +++
        self._serial.reset_input_buffer()
        self._serial.write(b"ATH\r")
        self._serial.flush()
        time.sleep(0.5)
        self._serial.reset_input_buffer()
        log.info("Modem hung up")

    def _send_at(self, cmd: str, timeout: float = 3.0) -> str:
        """Send an AT command and return the response text."""
        self._serial.reset_input_buffer()
        self._serial.write((cmd + "\r").encode("ascii"))
        self._serial.flush()

        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            n = self._serial.in_waiting
            if n:
                chunk = self._serial.read(n)
                if chunk:
                    buf += chunk
                    text = buf.decode("ascii", errors="replace")
                    for sentinel in ("OK", "ERROR", "CONNECT", "NO CARRIER",
                                     "NO DIALTONE", "BUSY", "NO ANSWER"):
                        if sentinel in text:
                            return text.strip()
            else:
                time.sleep(0.05)
        return buf.decode("ascii", errors="replace").strip()

    def init_modem(self) -> None:
        """Send initialization AT commands."""
        log.info("Initializing modem...")

        resp = self._send_at("ATZ", timeout=5.0)
        log.info("ATZ -> %s", resp.replace("\r\n", " ").strip())
        if "OK" not in resp:
            raise RuntimeError("ATZ failed: %r" % resp)

        resp = self._send_at("ATE0")
        log.info("ATE0 -> %s", resp.replace("\r\n", " ").strip())

        resp = self._send_at("ATV1")
        log.info("ATV1 -> %s", resp.replace("\r\n", " ").strip())

        if self.direct:
            log.info("Direct connect mode - will send ATA to answer")
        elif self.manual_answer:
            log.info("Manual answer mode - will send ATA on RING")
        else:
            resp = self._send_at("ATS0=1")
            log.info("ATS0=1 -> %s", resp.replace("\r\n", " ").strip())
            if "OK" not in resp:
                raise RuntimeError("ATS0=1 failed: %r" % resp)

        log.info("Modem initialized, waiting for call...")

    def wait_for_connect(self) -> bool:
        """
        Block until CONNECT is received from the modem.

        Modes:
        - direct: Send ATA to go off-hook in answer mode.  Retry on
          NO CARRIER (S7 timeout).  Works without a ring signal: the
          modem listens for the originating carrier from the Saturn.
        - manual_answer: Wait for RING, then send ATA.
        - default (ATS0=1): Wait for auto-answer on RING.

        Returns True on CONNECT, False on hard failure.
        """
        if self.direct:
            return self._wait_direct()

        buf = b""
        while True:
            n = self._serial.in_waiting
            if n:
                chunk = self._serial.read(n)
                if not chunk:
                    return False
                buf += chunk
                text = buf.decode("ascii", errors="replace")

                if "RING" in text:
                    if self.manual_answer:
                        log.info("RING detected, answering with ATA...")
                        self._serial.write(b"ATA\r")
                        self._serial.flush()
                        buf = b""
                    else:
                        log.info("RING detected, auto-answering...")

                if "CONNECT" in text:
                    time.sleep(0.1)
                    while True:
                        time.sleep(0.05)
                        n2 = self._serial.in_waiting
                        if not n2:
                            break
                        extra = self._serial.read(n2)
                        if not extra:
                            break
                        buf += extra
                    text = buf.decode("ascii", errors="replace")
                    nl = text.find("\n", text.find("CONNECT"))
                    if nl >= 0 and nl + 1 < len(buf):
                        self._leftover = buf[nl + 1:]
                    log.info("CONNECT: %s", text.strip().replace("\r\n", " "))
                    return True

                if "NO CARRIER" in text or "ERROR" in text:
                    log.warning("Modem error during wait: %s", text.strip())
                    return False
            else:
                time.sleep(0.1)

    def _wait_direct(self) -> bool:
        """
        Direct connect: send ATA to go off-hook and wait for carrier.

        Without a phone exchange or line voltage inducer, there is no
        ring signal.  ATA puts the modem in answer mode immediately --
        it goes off-hook and sends answer-mode carrier tones.  When the
        Saturn dials, both sides converge on CONNECT.  If the Saturn
        hasn't dialed yet, the modem's S7 register times out (~50s)
        and sends NO CARRIER; we just retry ATA.
        """
        while True:
            self._serial.reset_input_buffer()
            log.info("Sending ATA (direct answer mode)...")
            self._serial.write(b"ATA\r")
            self._serial.flush()

            buf = b""
            while True:
                n = self._serial.in_waiting
                if n:
                    chunk = self._serial.read(n)
                    if not chunk:
                        return False
                    buf += chunk
                    text = buf.decode("ascii", errors="replace")

                    if "CONNECT" in text:
                        time.sleep(0.1)
                        while True:
                            time.sleep(0.05)
                            n2 = self._serial.in_waiting
                            if not n2:
                                break
                            extra = self._serial.read(n2)
                            if not extra:
                                break
                            buf += extra
                        text = buf.decode("ascii", errors="replace")
                        nl = text.find("\n", text.find("CONNECT"))
                        if nl >= 0 and nl + 1 < len(buf):
                            self._leftover = buf[nl + 1:]
                        log.info("CONNECT: %s",
                                 text.strip().replace("\r\n", " "))
                        return True

                    if "NO CARRIER" in text:
                        log.info("ATA timed out (no carrier yet), retrying...")
                        break  # retry ATA

                    if "ERROR" in text:
                        log.warning("ATA error: %s", text.strip())
                        return False
                else:
                    time.sleep(0.1)


class MockModemHandler:
    """
    Simulates a modem by accepting a TCP connection.
    Used for testing the bridge without hardware.
    """

    def __init__(self, port: int = DEFAULT_MOCK_PORT):
        self.listen_port = port
        self._listen_sock = None
        self._client_sock = None

    def open(self) -> None:
        self._listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listen_sock.bind(("127.0.0.1", self.listen_port))
        self._listen_sock.listen(1)
        # Record actual port (useful when port=0)
        self.listen_port = self._listen_sock.getsockname()[1]
        log.info("Mock modem listening on 127.0.0.1:%d", self.listen_port)

    def close(self) -> None:
        if self._client_sock:
            try:
                self._client_sock.close()
            except OSError:
                pass
            self._client_sock = None
        if self._listen_sock:
            try:
                self._listen_sock.close()
            except OSError:
                pass
            self._listen_sock = None

    def read(self, size: int = 4096) -> bytes:
        try:
            return self._client_sock.recv(size)
        except BlockingIOError:
            return b""

    def write(self, data: bytes) -> None:
        self._client_sock.sendall(data)

    def hangup(self) -> None:
        """Close mock client connection (simulates hangup)."""
        if self._client_sock:
            try:
                self._client_sock.close()
            except OSError:
                pass
            self._client_sock = None
        log.info("Mock modem: hung up")

    def init_modem(self) -> None:
        log.info("Mock modem ready (no AT init needed)")

    def wait_for_connect(self) -> bool:
        """Accept a TCP connection (simulates CONNECT)."""
        log.info("Mock modem: waiting for TCP connection on port %d...",
                 self.listen_port)
        try:
            ready, _, _ = select.select([self._listen_sock], [], [], None)
            if ready:
                self._client_sock, addr = self._listen_sock.accept()
                self._client_sock.setblocking(False)
                log.info("Mock modem: CONNECT from %s:%d", addr[0], addr[1])
                return True
        except OSError:
            pass
        return False


# ---------------------------------------------------------------------------
# Bridge orchestrator
# ---------------------------------------------------------------------------

class NetlinkBridge:
    """
    Orchestrates the modem-to-TCP relay.

    Lifecycle: init modem -> wait for call -> connect to server -> relay
                           -> cleanup -> loop
    """

    def __init__(self, modem, server_host: str, server_port: int,
                 verbose: bool = False):
        self.modem = modem
        self.server_host = server_host
        self.server_port = server_port
        self.verbose = verbose
        self._server_sock = None
        self._running = False
        # Tail buffer for NO CARRIER detection on serial data
        self._modem_tail = b""

    def connect_to_server(self) -> bool:
        """Open TCP connection to the game's server.  No auth -- any
        handshake is the game's protocol, not the bridge's concern."""
        try:
            self._server_sock = socket.socket(socket.AF_INET,
                                              socket.SOCK_STREAM)
            self._server_sock.connect((self.server_host, self.server_port))
            log.info("Connected to server %s:%d",
                     self.server_host, self.server_port)
            self._server_sock.setblocking(False)
            return True
        except OSError as e:
            log.error("Failed to connect to server %s:%d: %s",
                      self.server_host, self.server_port, e)
            if self._server_sock:
                self._server_sock.close()
                self._server_sock = None
            return False

    def _disconnect_server(self) -> None:
        if self._server_sock:
            try:
                self._server_sock.close()
            except OSError:
                pass
            self._server_sock = None

    @staticmethod
    def check_no_carrier(tail: bytes) -> bool:
        """
        Check if the modem tail buffer contains NO CARRIER.
        Handles the pattern split across multiple reads.
        """
        text = tail.decode("ascii", errors="replace")
        return "NO CARRIER" in text

    def relay_loop(self) -> str:
        """
        Bidirectional byte relay between modem and server.
        Returns a reason string: "no_carrier", "server_closed", "error".

        Uses non-blocking reads on the modem (cross-platform) and
        select() only on the server socket (works on all platforms
        including Windows).
        """
        self._modem_tail = b""
        server_fd = self._server_sock.fileno()
        idle_seconds = 0.0

        while True:
            had_data = False

            # --- Modem -> Server (non-blocking poll) ---
            try:
                data = self.modem.read(4096)
            except OSError:
                return "no_carrier"

            if data:
                had_data = True
                if self.verbose:
                    log.debug("MODEM->SERVER %d bytes: %s",
                              len(data), data.hex())

                self._modem_tail = (self._modem_tail + data)[-NO_CARRIER_WINDOW:]
                if self.check_no_carrier(self._modem_tail):
                    log.info("NO CARRIER detected")
                    return "no_carrier"

                try:
                    self._server_sock.sendall(data)
                except OSError:
                    return "server_closed"

            # --- Server -> Modem (select on socket is cross-platform) ---
            try:
                readable, _, _ = select.select([server_fd], [], [], 0)
            except (ValueError, OSError):
                return "error"

            if readable:
                had_data = True
                try:
                    data = self._server_sock.recv(4096)
                except OSError:
                    return "server_closed"
                if not data:
                    return "server_closed"

                if self.verbose:
                    log.debug("SERVER->MODEM %d bytes: %s",
                              len(data), data.hex())

                try:
                    self.modem.write(data)
                except OSError:
                    return "no_carrier"

            if not had_data:
                time.sleep(0.01)
                idle_seconds += 0.01
                if idle_seconds >= 10.0:
                    log.debug("Relay idle %.0fs, no data from either side",
                              idle_seconds)
                    idle_seconds = 0.0
            else:
                idle_seconds = 0.0

    def run(self) -> None:
        """Main loop: init modem, wait for calls, relay, reconnect."""
        self._running = True
        try:
            self.modem.open()
            self.modem.init_modem()

            while self._running:
                log.info("Waiting for incoming call...")
                if not self.modem.wait_for_connect():
                    log.warning("wait_for_connect failed, retrying in 2s...")
                    time.sleep(2)
                    continue

                if not self.connect_to_server():
                    log.error("Cannot reach server, dropping modem connection")
                    self.modem.hangup()
                    time.sleep(2)
                    continue

                log.info("Relay active")
                reason = self.relay_loop()
                log.info("Relay ended: %s", reason)

                self._disconnect_server()
                self._modem_tail = b""

                if reason == "no_carrier":
                    log.info("Saturn disconnected, returning to wait...")
                elif reason == "server_closed":
                    log.warning("Server closed connection, hanging up modem")
                    self.modem.hangup()
                else:
                    log.warning("Relay error, retrying...")
                    time.sleep(1)

        except KeyboardInterrupt:
            log.info("Shutting down (keyboard interrupt)")
        finally:
            self._running = False
            self._disconnect_server()
            self.modem.close()
            log.info("Bridge shut down.")

    def stop(self) -> None:
        """Signal the bridge to stop."""
        self._running = False


# ---------------------------------------------------------------------------
# Serial port discovery
# ---------------------------------------------------------------------------

def list_serial_ports() -> list:
    """List available serial ports."""
    try:
        from serial.tools.list_ports import comports
        return list(comports())
    except ImportError:
        log.error("pyserial not installed. Run: pip install pyserial")
        return []


def auto_detect_modem() -> str:
    """Try to find a USB modem serial port."""
    ports = list_serial_ports()
    for p in ports:
        desc = (p.description or "").lower()
        if any(kw in desc for kw in ("modem", "usb", "serial", "uart")):
            return p.device
    # Fallback: return first port if any
    if ports:
        return ports[0].device
    return ""


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_server_arg(s: str) -> tuple:
    """Parse 'host:port' string."""
    if ":" in s:
        host, port_str = s.rsplit(":", 1)
        return host, int(port_str)
    return s, 4821


def main() -> None:
    parser = argparse.ArgumentParser(
        description="saturn-io NetLink Modem-to-TCP Bridge"
    )
    parser.add_argument(
        "--serial-port",
        help="Serial port for USB modem (auto-detects if omitted)",
    )
    parser.add_argument(
        "--baud", type=int, default=DEFAULT_BAUD,
        help="Serial baud rate (default: %d)" % DEFAULT_BAUD,
    )
    parser.add_argument(
        "--server", default=DEFAULT_SERVER,
        help="Game server host:port (default: %s)" % DEFAULT_SERVER,
    )
    parser.add_argument(
        "--mock", nargs="?", const=DEFAULT_MOCK_PORT, type=int, metavar="PORT",
        help="Mock mode: accept TCP instead of serial "
             "(default port: %d)" % DEFAULT_MOCK_PORT,
    )
    parser.add_argument(
        "--direct", action="store_true",
        help="Direct connect: send ATA without waiting for RING "
             "(for cable connections without a line voltage inducer)",
    )
    parser.add_argument(
        "--manual-answer", action="store_true",
        help="Use RING+ATA instead of ATS0=1 auto-answer",
    )
    parser.add_argument(
        "--verbose", action="store_true",
        help="Log every relayed byte (hex dump)",
    )
    parser.add_argument(
        "--list-ports", action="store_true",
        help="List available serial ports and exit",
    )
    args = parser.parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    if args.list_ports:
        ports = list_serial_ports()
        if not ports:
            print("No serial ports found.")
        else:
            print("Available serial ports:")
            for p in ports:
                print("  %s  --  %s" % (p.device, p.description))
        sys.exit(0)

    server_host, server_port = parse_server_arg(args.server)

    if args.mock is not None:
        modem = MockModemHandler(port=args.mock)
    else:
        port = args.serial_port or auto_detect_modem()
        if not port:
            log.error("No serial port found. Use --serial-port "
                      "or --list-ports.")
            sys.exit(1)
        modem = ModemHandler(port=port, baud=args.baud,
                             manual_answer=args.manual_answer,
                             direct=args.direct)

    bridge = NetlinkBridge(
        modem=modem,
        server_host=server_host,
        server_port=server_port,
        verbose=args.verbose,
    )
    bridge.run()


if __name__ == "__main__":
    main()
