// SPDX-License-Identifier: MIT
#ifndef TAILADB_ANDROID17_PROFILE_H
#define TAILADB_ANDROID17_PROFILE_H

// A ROM label is not sufficient to approve the daemon ABI or authentication boundary.
// ROM hashes are recorded below; runtime authentication and isolation are still unverified.
static const struct tailadb_daemon_profile tailadb_android17 = {
    .label = "Android 17 / SM-S948Q BZI9 (pending verification)",
    .verified = false,
    .adbd_sha256 = "d4d3141261f2f6c68d3effcf2c2aaf7a8167a3d882011fe51cd8248b5fa8a501",
    .auth_sha256 = "46bff7ced0447286293b5597b43a660db0ae3a954dd9a5db9725bf6bb6cb6a6e",
};

#endif
