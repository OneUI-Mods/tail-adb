// SPDX-License-Identifier: MIT
#ifndef TAILADB_ANDROID17_LEGACY_ADBD_PROFILE_H
#define TAILADB_ANDROID17_LEGACY_ADBD_PROFILE_H

// Observed on Android 17: the active Mainline module has the same daemon bytes
// audited on Android 16, while the system auth library is Android 17.
// This is an explicit pair, never a fallback that accepts arbitrary mixtures.
static const struct tailadb_daemon_profile tailadb_android17_legacy_adbd = {
    .label = "Android 17 / SM-S948Q BZI9 with retained Mainline adbd",
    .policy_abi = "android16",
    .verified = true,
    .adbd_sha256 = "459d1f67760789bdf8eb5a7a5c1bbe409d5dd3837d408ce4d7d14e496fcb8cbb",
    .auth_sha256 = "46bff7ced0447286293b5597b43a660db0ae3a954dd9a5db9725bf6bb6cb6a6e",
};

#endif
