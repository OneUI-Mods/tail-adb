"""Companion recovery policy and real bounded TCP protocol tests; no Android/root operations."""

import argparse
import importlib.util
from pathlib import Path
import socket
import sys
import threading
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "tailadb_watch", Path(__file__).resolve().parents[1] / "tools/tailadb_watch.py"
)
watch = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = watch
spec.loader.exec_module(watch)


class Server:
    def __init__(self, probes):
        self.probes = iter(probes)
        self.actions = []

    def start(self):
        self.actions.append("start")

    def close(self):
        self.actions.append("close")

    def probe(self):
        return next(self.probes)

    def reconnect(self):
        self.actions.append("reconnect")


class WatchTest(unittest.TestCase):
    def test_endpoint_rejects_lan_shell_and_invalid_ports(self):
        for endpoint in ["127.0.0.1:5555", "192.168.1.2:5555", "100.64.0.10:1", "x;whoami:5555"]:
            with self.assertRaises(argparse.ArgumentTypeError):
                watch.endpoint(endpoint)
        self.assertEqual(watch.endpoint("100.64.0.10:5555"), ("100.64.0.10", 5555))

    def test_other_host_and_network_wait_never_touch_adb(self):
        server = Server([])
        recovery = watch.Recovery()
        for state in ["OTHER_HOST", "WAITING"]:
            self.assertEqual(recovery.step(state, server, 100), state)
        self.assertEqual(server.actions, [])

    def test_live_transport_survives_network_handover(self):
        server = Server([True, True])
        recovery = watch.Recovery()
        for now in [100, 200]:
            self.assertEqual(recovery.step("CHECK_ADB_SERVER", server, now), "CONNECTED")
        self.assertNotIn("close", server.actions)
        self.assertNotIn("reconnect", server.actions)

    def test_reconnect_before_server_restart(self):
        server = Server([False, True])
        self.assertEqual(watch.Recovery().step("READY", server, 100), "RECONNECTED")
        self.assertEqual(server.actions, ["start", "reconnect"])

    def test_persistently_stuck_adb_restarts_only_owned_server(self):
        server = Server([False] * 4)
        recovery = watch.Recovery()
        self.assertEqual(recovery.step("CHECK_ADB_SERVER", server, 100), "ADB_NOT_RESPONDING")
        self.assertEqual(recovery.step("CHECK_ADB_SERVER", server, 105), "SERVER_RESTARTED")
        self.assertEqual(server.actions.count("close"), 1)

    def test_restart_cooldown_and_healthy_reset(self):
        server = Server([False] * 8 + [True])
        recovery = watch.Recovery()
        for now in [100, 105, 110, 115]:
            recovery.step("READY", server, now)
        self.assertEqual(server.actions.count("close"), 1)
        self.assertEqual(recovery.step("READY", server, 120), "CONNECTED")
        self.assertEqual(recovery.failures, 0)

    def receive(self, payload):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]

        def reply():
            try:
                with listener.accept()[0] as client:
                    client.sendall(payload)
            finally:
                listener.close()

        thread = threading.Thread(target=reply, daemon=True)
        thread.start()
        try:
            return watch.phone_health("127.0.0.1", port)
        finally:
            thread.join(timeout=3)

    def test_actual_tcp_health_record(self):
        self.assertEqual(self.receive(b"TAILADB_HEALTH\t1\tCHECK_ADB_SERVER\t42\n"),
                         ("CHECK_ADB_SERVER", 42))

    def test_malformed_and_unauthorized_tcp_are_not_ready(self):
        for record in [b"", b"HELLO\n", b"TAILADB_HEALTH\t2\tREADY\t1\n",
                       b"TAILADB_HEALTH\t1\tREADY\t1\nextra", b"x" * 128]:
            with self.assertRaises(ValueError):
                self.receive(record)

    def test_owned_process_only_never_global_kill_server(self):
        server = watch.OwnedServer("adb", "100.64.0.10:5555")
        with patch.object(watch.subprocess, "Popen") as popen:
            child = popen.return_value
            child.poll.return_value = None
            with patch.object(watch.socket, "create_connection"):
                server.start()
            command = popen.call_args.args[0]
            self.assertEqual(command[1], "-L")
            self.assertTrue(command[2].startswith("tcp:localhost:"))
            self.assertEqual(command[-2:], ["server", "nodaemon"])
            self.assertIn("--one-device", command)
            self.assertEqual(popen.call_args.kwargs["env"]["ADB_MDNS_AUTO_CONNECT"], "0")
            server.close()
            child.terminate.assert_called_once()
            self.assertNotIn("kill-server", command)


if __name__ == "__main__":
    unittest.main()
