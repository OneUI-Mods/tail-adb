# 開発者向けガイド

## ビルド

JDK 21、Android SDK 36、NDK 28.2.13676358を用意し、Android Studioで開きます。作業フォルダは `~/Documents/TailAdb`、パッケージは `net.ryuya_dev.tailadb` です。

```sh
./gradlew :app:assembleRelease :app:lintRelease
```

鍵がない場合は未署名APKを生成します。配布にはdebug鍵を使いません。リポジトリ外のpropertiesファイルに `storeFile`、`storePassword`、`keyAlias`、`keyPassword` を記入し、次の環境変数で指定します。

```sh
export TAILADB_SIGNING_PROPERTIES=/absolute/path/to/private-signing.properties
./gradlew :app:assembleRelease :app:lintRelease
```

署名済み出力：`app/build/outputs/apk/release/app-release.apk`。ローカルの配布鍵は `~/.android/keystores/tail-adb-release.jks`、設定は同ディレクトリの `tail-adb-signing.properties` です。更新のために安全に保管・バックアップしてください。鍵・パスワードをGitへ追加してはいけません。

配布署名証明書SHA-256：`429de1280138eb96207ea61c6c19a36d2a83404e4654ce52d14e1aba43abf980`。

## 実装

本体は通常のAndroidアプリです。Xposedは必要ありません。前景サービスが、許可された `su` を通じて専用のnative supervisorを実行します。配布APKには自作のsupervisorとプロセス内の設定shimを含め、Samsungのadbdバイナリを含めません。

起動時に端末自身の `/apex/com.android.adbd/bin/adbd` を読み取り、検証済みハッシュと一致する場合だけroot所有のprivate作業ディレクトリへコピーします。APEXのリンカー領域が外部shimのロードを許可しないため、コピーだけを通常のリンカー領域で動かし、APEXの共有ライブラリは読み取り参照します。元ファイル、system/vendor/product/boot、システムのプロパティ領域、root実装は変更しません。

root側からConnectivityServiceの現在のVPN情報を読み、Tailscaleのパッケージ名・実際の所有UID・インターフェース・CGNAT /32アドレスを確認します。曖昧な構造や複数候補は拒否します。別プロファイルのVPNも、rootから取得した実際のUIDとネットワークハンドルで扱います。単にtun0や100.xという名前・アドレスだけでは信用しません。

supervisorは `SO_BINDTODEVICE` とアドレス指定、`android_setsocknetwork()` でTailscale専用のソケットを先に作ります。子プロセスは別のnetwork namespaceへ隔離し、このソケットだけを引き継ぎます。adbdの実装にある `acceptfd:` で受け渡すため、別のネットワーク待受をホスト側に作れません。子の標準入力は切り離し、無関係な継承FDも閉じます。

プロセス内shimは追加adbdだけの設定補助です。`service.adb.listen_addrs` を `acceptfd:` にし、USB利用・追加TLSサーバー・root権限での待受を無効にします。通常のプロパティ領域へ書き込みません。shimが読み込めない、隔離できない、UID 2000へ降格できない、親との起動確認が成立しない場合は開始を取りやめます。

Android自身のRSA ADB認証を維持し、`/data/misc/adb/adb_keys` の承認キーを使います。新しいキーの自動登録や認証無効化はしません。承認キーの変更をinotifyで受けて接続プロセスを作り直し、既存の接続も終了させます。監視ディレクトリ自体が削除・移動された場合は安全のため終了します。Frameworkの認証ソケットへは接続しないため、追加待受から承認ダイアログは出せません。

終了は前景サービスの制御パイプ、シグナル、親死亡時のSIGKILLで連動します。UID降格で解除される親死亡通知は降格後に設定し直します。子の異常終了が3回に達すると停止します。VPNの更新はnetlinkイベントを受け、イベントで把握できない所有者の変化を15秒間隔の低頻度確認で補います。VPNインターフェース以外への接続はソケット自身の拘束で拒否します。

## 対応したバイナリ

SM-S948Q / Android 16、ビルド `BP4A.251205.006.S948QOPS1AZHL`。

adbd SHA-256：`459d1f67760789bdf8eb5a7a5c1bbe409d5dd3837d408ce4d7d14e496fcb8cbb`。

未知のadbdは安全のため拒否します。OS更新への対応には、標準の認証・権限降格・acceptfdの動作を新しいバイナリで確認してから基準を更新してください。ハッシュだけを増やして対応済みと扱わないでください。

## 参照した実装

- [AOSP adbd main.cpp / Android 16](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android16-release/daemon/main.cpp)
- [AOSP socket_spec.cpp / acceptfd](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android16-release/socket_spec.cpp)
- [AOSP libadbd_auth](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android16-release/libs/adbd_auth/adbd_auth.cpp)
- [Tailscale AndroidのVPN実装](https://github.com/tailscale/tailscale-android/blob/main/libtailscale/net.go)

調査用の端末バイナリ、個人ログ、実際のIP・tailnet名、スクリーンショットは `artifacts/` に置き、公開しません。依存ライセンスの一覧と実際のruntime依存が異なる場合はビルドを停止します。
