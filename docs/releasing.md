# 配布用ビルド

JDK 21 / SDK 37 / NDK 28.2.13676358で`python3 tools/package_release.py --source-ref HEAD`を実行する。
アプリとnativeソースは先にコミットする。署名propertiesは`TAILADB_SIGNING_PROPERTIES`で指定し、未指定時は既存の`~/.android/keystores/tail-adb-signing.properties`を使う。鍵・パスワード・propertiesはGitとログへ含めない。

release variant、SDK 37、非debuggable、検証Activity非混入、arm64/x86_64のnativeライブラリ、16 KiB alignment、既存配布証明書を検証する。依存ライセンスはGradleの実runtime依存照合を使う。Android 17 validationビルドは配布しない。

配布物は`artifacts/release/TailAdb-<version>-release.apk`、対応するsource ZIP、`tailadb_watch.py`、SHA256SUMS。リリース文は外部向けに使い勝手・対応環境・更新手順・既知の制限を書く。実機ログ・個人のIP・内部経緯は開発文書とGit管理外のartifactsへ分ける。公開済みタグと添付APKは移動・差し替えしない。
