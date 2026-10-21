#!/usr/bin/env python3
"""Recover one Tail ADB connection using a separately owned, loopback-only ADB server."""

import argparse
import ipaddress
import logging
import os
import shutil
import signal
import socket
import subprocess
import time
from dataclasses import dataclass


LOG = logging.getLogger("tailadb")
STATES = {"READY", "CHECK_ADB_SERVER", "OTHER_HOST", "WAITING"}


def endpoint(value):
    try:
        address, port = value.rsplit(":", 1)
        ip = ipaddress.IPv4Address(address)
        port = int(port)
        if ip not in ipaddress.IPv4Network("100.64.0.0/10") or not 1024 <= port <= 65535:
            raise ValueError()
        return str(ip), port
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "Use a Tailscale IPv4:port, e.g. 100.64.0.10:5555"
        ) from error


def phone_health(address, port):
    # The server sends one small record immediately. No command or secret is transmitted.
    with socket.create_connection((address, port), timeout=2) as connection:
        connection.settimeout(2)
        record = bytearray()
        while b"\n" not in record and len(record) < 128:
            chunk = connection.recv(128 - len(record))
            if not chunk:
                break
            record.extend(chunk)
    fields = record.decode("ascii").rstrip("\n").split("\t")
    if (
        len(fields) != 4
        or fields[:2] != ["TAILADB_HEALTH", "1"]
        or fields[2] not in STATES
        or not fields[3].isdigit()
        or not record.endswith(b"\n")
        or record.count(b"\n") != 1
    ):
        raise ValueError("Unrecognized Tail ADB health record")
    return fields[2], int(fields[3])


class OwnedServer:
    """Only this foreground child is ever terminated; never calls adb kill-server."""

    def __init__(self, adb, device):
        self.adb = adb
        self.device = device
        self.process = None
        self.port = None
        self.environment = os.environ.copy()
        for name in ("ADB_SERVER_SOCKET", "ANDROID_ADB_SERVER_PORT", "ANDROID_ADB_SERVER_ADDRESS"):
            self.environment.pop(name, None)
        self.environment["ADB_MDNS_AUTO_CONNECT"] = "0"

    def start(self):
        if self.process is not None and self.process.poll() is None:
            return
        # Pick a fresh private port after each restart. Never adopt a pre-existing server.
        with socket.socket() as temporary:
            temporary.bind(("127.0.0.1", 0))
            self.port = temporary.getsockname()[1]
        self.process = subprocess.Popen(
            [
                self.adb,
                "-L",
                f"tcp:localhost:{self.port}",
                "--one-device",
                self.device,
                "server",
                "nodaemon",
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env=self.environment,
        )
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise OSError("Dedicated ADB server exited during startup")
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=0.1):
                    LOG.info("Dedicated ADB server: adb -P %s -s %s", self.port, self.device)
                    return
            except OSError:
                time.sleep(0.05)
        self.close()
        raise TimeoutError("Dedicated ADB server did not start")

    def connection(self):
        if self.process is None or self.process.poll() is not None:
            raise OSError("Dedicated ADB server is not running")
        connection = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        connection.settimeout(2)
        return connection

    @staticmethod
    def receive(connection, length):
        data = bytearray()
        while len(data) < length:
            chunk = connection.recv(length - len(data))
            if not chunk:
                raise OSError("ADB server closed the request")
            data.extend(chunk)
        return bytes(data)

    def request(self, connection, service):
        payload = service.encode("ascii")
        connection.sendall(f"{len(payload):04x}".encode("ascii") + payload)
        if self.receive(connection, 4) != b"OKAY":
            raise OSError("ADB server rejected the request")

    def host(self, service):
        # Speak the documented smart-socket protocol directly. Unlike invoking an adb client,
        # this cannot auto-start/adopt an unowned server if the owned process dies mid-request.
        with self.connection() as connection:
            self.request(connection, service)
            length = int(self.receive(connection, 4), 16)
            if length > 4096:
                raise ValueError("ADB response exceeds the protocol bound")
            return self.receive(connection, length).decode("ascii")

    def probe(self):
        try:
            with self.connection() as connection:
                self.request(connection, "host:transport:" + self.device)
                self.request(connection, "shell:echo TAILADB_OK")
                output = bytearray()
                while len(output) <= 256:
                    data = connection.recv(257 - len(output))
                    if not data:
                        return output.strip() == b"TAILADB_OK"
                    output.extend(data)
                return False
        except (OSError, ValueError):
            return False

    def reconnect(self):
        try:
            self.host("host:disconnect:" + self.device)
        except OSError:
            # An absent old transport is a normal first-connect state, not a recovery failure.
            pass
        return "connected to" in self.host("host:connect:" + self.device)

    def pending_auth(self):
        for line in self.host("host:devices").splitlines():
            fields = line.split()
            if len(fields) >= 2 and fields[0] == self.device:
                return fields[1] == "unauthorized"
        return False

    def close(self):
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
            self.process = None


@dataclass
class Recovery:
    failures: int = 0
    restart_after: float = 0
    auth_since: float | None = None

    def step(self, state, server, now):
        if state in ("WAITING", "OTHER_HOST"):
            # A different host owns the single transport. Never evict it to run a probe.
            return state
        server.start()
        if server.probe():
            self.failures = 0
            self.auth_since = None
            return "CONNECTED"
        try:
            if server.pending_auth():
                if self.auth_since is None:
                    self.auth_since = now
                if now - self.auth_since < 20:
                    return "WAITING_FOR_ADB_AUTH"
            server.reconnect()
            if server.probe():
                self.failures = 0
                self.auth_since = None
                return "RECONNECTED"
            if server.pending_auth():
                if self.auth_since is None:
                    self.auth_since = now
                if now - self.auth_since < 20:
                    return "WAITING_FOR_ADB_AUTH"
        except (OSError, subprocess.TimeoutExpired):
            # A wedged host server can hang even its disconnect command. This is an ADB failure,
            # not an unreachable phone: count it so the owned server is eventually replaced.
            pass
        self.failures += 1
        if self.failures >= 2 and now >= self.restart_after:
            server.close()
            server.start()
            try:
                server.reconnect()
            except (OSError, subprocess.TimeoutExpired):
                pass
            self.restart_after = now + 30
            self.failures = 0
            self.auth_since = None
            return "SERVER_RESTARTED"
        return "ADB_NOT_RESPONDING"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True, type=endpoint)
    parser.add_argument("--adb", default=shutil.which("adb"))
    parser.add_argument("--once", action="store_true", help="Check once and exit")
    args = parser.parse_args()
    if not args.adb:
        parser.error("Install Android Platform Tools or pass --adb /path/to/adb")
    address, port = args.device
    health_port = 65534 if port == 65535 else port + 1
    server = OwnedServer(args.adb, f"{address}:{port}")
    recovery = Recovery()
    previous = None
    delay = 1
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")

    def stop(_signal, _frame):
        raise KeyboardInterrupt()

    signal.signal(signal.SIGTERM, stop)
    try:
        while True:
            try:
                state, _generation = phone_health(address, health_port)
                status = recovery.step(state, server, time.monotonic())
                delay = 5 if status == "CONNECTED" else min(delay * 2, 30)
            except (OSError, ValueError, subprocess.TimeoutExpired):
                status = "WAITING_FOR_PHONE"
                # No restart storm during Wi-Fi/mobile loss, VPN reconnection, or revoked access.
                delay = min(delay * 2, 30)
            if status != previous:
                LOG.info(status)
                previous = status
            if args.once:
                return 0 if status in ("CONNECTED", "RECONNECTED") else 2
            time.sleep(delay)
    except KeyboardInterrupt:
        return 0
    finally:
        server.close()


if __name__ == "__main__":
    raise SystemExit(main())
