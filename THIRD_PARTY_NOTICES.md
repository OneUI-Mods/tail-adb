# Third-party notices

Tail ADB is MIT licensed. Its bundled Android libraries and transitive runtime dependencies are listed in `third_party/components.tsv`, with the corresponding full license texts in `third_party/`. The build verifies this inventory and includes the full texts in the app's license screen.

AndroidX and Material: The Android Open Source Project and contributors. Kotlin, coroutines and annotations: JetBrains and contributors. Guava listenablefuture: Google and contributors.

Tailscale and the device's adbd are not bundled in the APK. The backend uses the already installed Tailscale VPN and makes a private runtime copy of the device's own adbd, without changing its original file.
