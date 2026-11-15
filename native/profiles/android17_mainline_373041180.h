// SPDX-License-Identifier: MIT
#ifndef TAILADB_ANDROID17_MAINLINE_373041180_PROFILE_H
#define TAILADB_ANDROID17_MAINLINE_373041180_PROFILE_H

// Keep each Mainline daemon/auth-library pair independent of the OS build label.
// Verified on Android 17: shell/root separation, no-new-privileges, SELinux,
// Tailscale-only binding, existing host authorization, and sync round trip.
static const struct tailadb_daemon_profile tailadb_android17_mainline_373041180 = {
    .label = "Android 17 / SM-S948Q BZI9 / Mainline ADB 373041180",
    .policy_abi = "android17",
    .verified = true,
    .adbd_sha256 = "37498581d2dc6f249120a4a9b01087a87013d7cdc63d05ac3d6ce198470dfefb",
    .auth_sha256 = "46bff7ced0447286293b5597b43a660db0ae3a954dd9a5db9725bf6bb6cb6a6e",
};

#endif
