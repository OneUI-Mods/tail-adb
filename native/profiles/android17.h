// SPDX-License-Identifier: MIT
#ifndef TAILADB_ANDROID17_PROFILE_H
#define TAILADB_ANDROID17_PROFILE_H

// A ROM label is not sufficient to approve the daemon ABI or authentication boundary.
// Populate and enable only after separate Android 17 verification.
static const struct tailadb_daemon_profile tailadb_android17 = {
    .label = "Android 17 / SM-S948Q BZI9 (pending verification)",
    .verified = false,
    .adbd_sha256 = "",
    .auth_sha256 = "",
};

#endif
