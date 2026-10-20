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
        raise argparse.ArgumentTypeError("Use a Tailscale IPv4:port, e.g. 100.64.0.10:5555") from error


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
            [self.adb, "-L", f"tcp:localhost:{self.port}", "--one-device", self.device,
             "server", "nodaemon"],
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

    def run(self, *arguments):
        if self.process is None or self.process.poll() is not None:
            raise OSError("Dedicated ADB server is not running")
        result = subprocess.run(
            [self.adb, "-H", "127.0.0.1", "-P", str(self.port), *arguments],
            stdin=subprocess.DEVNULL,
            capture_output=True,
            timeout=4,
            check=False,
            env=self.environment,
        )
        if result.returncode:
            return ""
        return result.stdout.decode("utf-8", errors="replace").strip()

    def probe(self):
        try:
            return self.run("-s", self.device, "shell", "echo", "TAILADB_OK") == "TAILADB_OK"
        except (OSError, subprocess.TimeoutExpired):
            return False

    def reconnect(self):
        self.run("disconnect", self.device)
        return "connected to" in self.run("connect", self.device)

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

    def step(self, state, server, now):
        if state in ("WAITING", "OTHER_HOST"):
            # A different host owns the single transport. Never evict it to run a probe.
            return state
        server.start()
        if server.probe():
            self.failures = 0
            return "CONNECTED"
        server.reconnect()
        if server.probe():
            self.failures = 0
            return "RECONNECTED"
        self.failures += 1
        if self.failures >= 2 and now >= self.restart_after:
            server.close()
            server.start()
            server.reconnect()
            self.restart_after = now + 30
            self.failures = 0
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
