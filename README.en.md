# Tail ADB

[日本語](README.md) · English

Tail ADB creates an independent ADB listener on a rooted Android device, reachable only over Tailscale. It works over Wi-Fi or mobile data even with Android's **Wireless debugging** setting off.

The listener binds to a verified Tailscale VPN interface and the device's Tailscale IPv4 address. It is not exposed on the LAN and does not change Android's stock listener or another app's ADB listener.

## Automatic startup after boot

Automatic startup after device boot can be switched on or off in settings. The default is on when no choice has been saved; an existing off choice is preserved. Startup only restores configured behavior and does not override feature switches, permissions, or lock-state restrictions.

## Features

- Start and stop a Tailscale-only ADB listener.
- Display and copy connection commands.
- Show first-time hosts and control ADB shell permission with a toggle.
- Start every ADB session as Android UID 2000, `shell`.
- Separately permit `su` root escalation per source Tailscale IP.
- Manage approved hosts and shell/root permissions in settings.
- Select the Android user or work profile running Tailscale.
- Show candidates when multiple Tailscale interfaces are found.
- Recreate the listener after network changes/recovery, with automatic retry on failure.
- Verify that Tailscale is actually connected in the selected Android user.
- Optional PC recovery command for cases where TCP connects but ADB does not respond.
- Optional automatic startup after a normal boot or soft reboot.
- Background operation without a persistent notification.
- Queue another PC while one is connected, avoiding rapid reconnect loops.
- Material 3 UI, Japanese/English, system-language following, and per-app language selection.

## Requirements

Physical-device validation covers Galaxy S26 Ultra `SM-S948Q` on Android 16 / One UI 8.5 (`BP4A.251205.006.S948QOPS1AZHL`) and Android 17 / One UI 9 (`CP2A.260605.016.S948QOPU1BZI9`, with verified Mainline ADB versions). Startup is refused if the OS `adbd` and authentication library do not match a verified binary pair.

You need:

- A compatible device running KernelSU with `--ksu-no-new-privs` support; validation used version code 32664.
- Tailscale installed on both the Android device and connecting PC.
- A PC able to reach the device in the same tailnet.
- Android Platform Tools (`adb`) on the PC.

No Xposed, LSPosed, or Vector scopes are required. Tail ADB does not root the device, unlock its bootloader, or log into Tailscale for you.

## Installation

Download the latest release APK from [GitHub Releases](https://github.com/OneUI-Mods/tail-adb/releases). Distributed APKs are signed with a dedicated release key.

```sh
adb install -r TailAdb-VERSION-release.apk
```

Root permission is requested automatically on first launch. If denied, the app shows an error; approve Tail ADB in your root manager.

## Usage

The Tailscale-only connection method also works on Android 17, while Android 16 support is retained. Android 17 has multiple ADB builds, so only verified daemon/authentication-library combinations are allowed. See [compatibility status (Japanese)](docs/compatibility/android17.md).

1. Connect the device and PC to Tailscale.
2. Open Tail ADB. If necessary, select the Android user running Tailscale in settings.
3. Choose **Start ADB**. The default port is 5555.
4. Run the displayed command on the PC.

```sh
adb connect 100.64.0.10:5555
```

This is an example address; use the address displayed by your app.

A first-time host is `unauthorized`, and its Tailscale IP appears as awaiting shell permission. Enable **Allow ADB shell connection** to safely restart the dedicated listener and admit the host. Every newly connected session starts with:

```text
uid=2000(shell) ... context=u:r:shell:s0
```

Enable **Allow root escalation** only for hosts that need root, then use `su -c <command>` inside ADB shell. Disabling root preserves shell access. Disabling shell also revokes root access. Review and edit saved grants under **Approved hosts** in settings.

The dedicated listener continues after the UI closes, without a persistent notification. Use **Stop** in the app to shut it down. With automatic startup enabled, the app attempts startup after a normal Android boot or soft reboot. If root is unavailable, it stops rather than repeatedly requesting permission or retrying.

## Connection checks and automatic recovery

Version 0.2.10 adds Android 17 Mainline ADB version 373041180. Android 16 and previously verified ADB versions remain supported; unknown pairs are still refused. Compatibility errors now clarify that they concern the daemon/authentication-library pair rather than the entire OS implementation.

Version 0.2.8 reduces idle work by lengthening checks while state is stable. Network changes bring checks forward. UI status polling also slows when unchanged.

Version 0.2.7 improves incorrectly reported authentication failures for approved PCs. If another PC is active, the new connection waits without interrupting it and takes over after disconnection.

Use **Check Tailscale connection for selected user** on the connection screen to verify that the configured Android user's Tailscale is connected. It distinguishes connection in another user only, disconnection, and failed verification. This check changes neither the destination nor permissions.

A network change preserves an active ADB session if the same Tailscale connection remains valid. Loss of the VPN closes the listener; retries are spaced out until connectivity returns. Commands running during an outage are not guaranteed to continue.

For automatic PC-side recovery, save the release asset `tailadb_watch.py` and use **Copy PC recovery command**. Python 3.10+ and recent Android Platform Tools are required.

```sh
python3 tailadb_watch.py --device 100.64.0.10:5555
```

First disconnect the ordinary ADB server from this device. The script starts a dedicated local ADB server and displays the appropriate `adb -P <port> -s <device>` command. It compares device-reported state with ADB responsiveness and restarts only its dedicated server when reconnecting does not resolve the issue. It never stops the ordinary server. Press Ctrl+C to exit. The dedicated server's port changes when restarted.

The status TCP endpoint uses the port after ADB's port, or 65534 when ADB uses 65535. It is Tailscale-only and readable only by shell-approved hosts. The phone does not directly restart the PC's ADB server; the script running on the PC performs recovery.

## Connectivity and permissions

The listener is Tailscale-only. Shell and root grants are independent per source, and unapproved hosts receive no shell. Failure to verify the Tailscale connection, selected user, or device compatibility stops the listener. See the [developer guide (Japanese)](docs/development.md) for boundaries and validation.

## Source and license

The code uses the [MIT License](LICENSE). Dependency licenses are in the app and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This is not an official Tailscale app.

See the [developer guide (Japanese)](docs/development.md) for builds, internals, and test environments, and [source style (Japanese)](docs/source-style.md) for formatting and checking original code.

## String compatibility

Handling of certain whitespace and empty strings in connection information, host lists, and saved candidates has been restored to the previous behavior.

[Build environment / ビルド環境](docs/build-environment.md)
