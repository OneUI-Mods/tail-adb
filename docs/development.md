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

本体は通常のAndroidアプリです。Xposedは必要ありません。画面から開始すると短時間の通常Serviceがroot権限を要求し、native supervisorを独立プロセスとして起動して終了します。supervisorは状態ファイルと明示的なstart/stop/statusコマンドで管理するため、前景Serviceや常駐通知に依存しません。配布APKには自作のsupervisor、プロセス内の設定shim、制限付き `su` クライアントを含め、Samsungのadbdバイナリを含めません。

起動時に端末自身の `/apex/com.android.adbd/bin/adbd` を読み取り、検証済みハッシュと一致する場合だけroot所有のprivate作業ディレクトリへコピーします。APEXのリンカー領域が外部shimのロードを許可しないため、コピーだけを通常のリンカー領域で動かし、APEXの共有ライブラリは読み取り参照します。元ファイル、system/vendor/product/boot、システムのプロパティ領域、root実装は変更しません。

root側からConnectivityServiceの現在のVPN情報を読み、Tailscaleのパッケージ名・実際の所有UID・インターフェース・CGNAT /32アドレスを確認します。曖昧な構造や複数候補は拒否します。別プロファイルのVPNも、rootから取得した実際のUIDとネットワークハンドルで扱います。単にtun0や100.xという名前・アドレスだけでは信用しません。

supervisorは `SO_BINDTODEVICE` とアドレス指定、`android_setsocknetwork()` でTailscale専用のソケットを先に作ります。子プロセスは別のnetwork namespaceとmount namespaceへ隔離し、このソケットだけを引き継ぎます。adbdの実装にある `acceptfd:` で受け渡すため、別のネットワーク待受をホスト側に作れません。子の標準入力は切り離し、無関係な継承FDも閉じます。FDフラグの設定や再確認に失敗した場合は続行しません。

プロセス内shimは追加adbdだけの設定補助です。`service.adb.listen_addrs` を `acceptfd:` にし、USB利用・追加TLSサーバー・root権限での待受を無効にします。通常のプロパティ領域へ書き込みません。起動直後にroot管理の `ksu` ドメインから標準の `adbd` ドメインへ移り、UID/GID 2000、capabilityなし、`u:r:adbd:s0` を確認してから初めてacceptを許可します。AOSPの `adbd` から `shell` への自動ドメイン遷移によって、接続先コマンドは `u:r:shell:s0` になります。shimが読み込めない、SELinux遷移・隔離・資格情報・親との制御接続のいずれかを確認できない場合は開始を取りやめます。

同時に受け付けるADBトランスポートを1つに限定し、accept時に接続元Tailscale IPv4と256-bitのセッショントークンをroot supervisorへ通知します。shell起動時だけトークンと乱数名のbrokerを環境へ渡します。mount namespace内では既存root管理の `su` パスを同梱クライアントへbind mountし、絶対パスでの迂回も防ぎます。brokerは接続元IPがアプリの許可リストにあること、トークン、UID/GID 2000、SELinux peer context `u:r:shell:s0`、渡された標準入出力FDをすべて確認してからrootコマンドを実行します。検査エラー、短い制御メッセージ、余分なancillary data、タイムアウトは拒否側に倒します。

Android自身のRSA ADB認証を維持し、`/data/misc/adb/adb_keys` の承認キーを使います。新しいキーの自動登録や認証無効化はしません。承認キーの変更をinotifyで受けて接続プロセスを作り直し、既存の接続も終了させます。監視ディレクトリ自体が削除・移動された場合は安全のため終了します。Frameworkの認証ソケットへは接続しないため、追加待受から承認ダイアログは出せません。

終了はアプリがroot helperの `--stop` を呼び、root所有PIDと実プロセスのUIDを検査してSIGTERMを送ります。追加adbdはsupervisorの親死亡時にSIGKILLされます。UID降格で解除される親死亡通知は降格後に設定し直します。子の異常終了が3回に達すると停止します。VPNの更新はnetlinkイベントを受け、イベントで把握できない所有者の変化を15秒間隔の低頻度確認で補います。VPNインターフェース以外への接続はソケット自身の拘束で拒否します。

## 対応したバイナリ

SM-S948Q / Android 16、ビルド `BP4A.251205.006.S948QOPS1AZHL`。

adbd SHA-256：`459d1f67760789bdf8eb5a7a5c1bbe409d5dd3837d408ce4d7d14e496fcb8cbb`。

未知のadbdは安全のため拒否します。OS更新への対応には、標準の認証・権限降格・acceptfdの動作を新しいバイナリで確認してから基準を更新してください。ハッシュだけを増やして対応済みと扱わないでください。

## 参照した実装

- [AOSP adbd main.cpp / Android 16](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android16-release/daemon/main.cpp)
- [AOSP socket_spec.cpp / acceptfd](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android16-release/socket_spec.cpp)
- [AOSP shell_service.cpp / shell process](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android16-release/daemon/shell_service.cpp)
- [AOSP libadbd_auth](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android16-release/libs/adbd_auth/adbd_auth.cpp)
- [Tailscale AndroidのVPN実装](https://github.com/tailscale/tailscale-android/blob/main/libtailscale/net.go)

調査用の端末バイナリ、個人ログ、実際のIP・tailnet名、スクリーンショットは `artifacts/` に置き、公開しません。依存ライセンスの一覧と実際のruntime依存が異なる場合はビルドを停止します。
