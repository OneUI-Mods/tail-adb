# 開発者向けガイド

## ビルド

JDK 21、Android SDK 37、NDK 28.2.13676358を用意し、Android Studioで開きます。作業フォルダは `~/Documents/TailAdb`、パッケージは `net.ryuya_dev.tailadb` です。

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

本体は通常のAndroidアプリです。Xposedは必要ありません。画面から開始すると短時間の通常Serviceがroot権限を要求し、KernelSUの`su --ksu-no-new-privs`を使ってnative supervisorを独立プロセスとして起動して終了します。supervisorは状態ファイルと明示的なstart/stop/statusコマンドで管理するため、前景Serviceや常駐通知に依存しません。配布APKには自作のsupervisor、プロセス内の設定shim、制限付き`su`クライアントを含め、Samsungのadbdバイナリを含めません。

自動起動の設定はdevice-protected storageにも保存します。`BOOT_COMPLETED` の受信後、BroadcastReceiverの短時間の非同期処理から既存のstartコマンドを呼びます。rootコマンドは8秒で打ち切り、root未導入・一時root消失・権限拒否時は `ERROR_AUTO_ROOT` を保存して終了します。再試行ループ、Foreground Service、常駐通知は使用しません。

起動時に端末自身の `/apex/com.android.adbd/bin/adbd` を読み取り、検証済みハッシュと一致する場合だけroot所有のprivate作業ディレクトリへコピーします。APEXのリンカー領域が外部shimのロードを許可しないため、コピーだけを通常のリンカー領域で動かし、APEXの共有ライブラリは読み取り参照します。元ファイル、system/vendor/product/boot、システムのプロパティ領域、root実装は変更しません。

root側からConnectivityServiceの現在のVPN情報を読み、Tailscaleのパッケージ名・実際の所有UID・インターフェース・CGNAT /32アドレスを確認します。曖昧な構造や複数候補は拒否します。別プロファイルのVPNも、rootから取得した実際のUIDとネットワークハンドルで扱います。単にtun0や100.xという名前・アドレスだけでは信用しません。

supervisorは `SO_BINDTODEVICE` とアドレス指定、`android_setsocknetwork()` でTailscale専用のソケットを先に作ります。子プロセスは別のnetwork namespaceとmount namespaceへ隔離し、このソケットだけを引き継ぎます。adbdの実装にある `acceptfd:` で受け渡すため、別のネットワーク待受をホスト側に作れません。子の標準入力は切り離し、無関係な継承FDも閉じます。FDフラグの設定や再確認に失敗した場合は続行しません。

プロセス内shimは追加adbdだけの設定補助です。`service.adb.listen_addrs` を `acceptfd:` にし、USB利用・追加TLSサーバー・root権限での待受を無効にします。通常のプロパティ領域へ書き込みません。起動直後にroot管理の `ksu` ドメインから標準の `adbd` ドメインへ移り、UID/GID 2000、capabilityなし、`u:r:adbd:s0` を確認してから初めてacceptを許可します。AOSPの `adbd` から `shell` への自動ドメイン遷移によって、接続先コマンドは `u:r:shell:s0` になります。shimが読み込めない、SELinux遷移・隔離・資格情報・親との制御接続のいずれかを確認できない場合は開始を取りやめます。

同時に受け付けるADBトランスポートを1つに限定し、accept時に接続元Tailscale IPv4と256-bitのセッショントークンをroot supervisorへ通知します。Samsung実装では低位のaccept済みFDがtransport登録時に閉じられたため、shimはsocketを`F_DUPFD_CLOEXEC`で128以上へ移してからadbdへ返します。plain `close()`とbionicの`android_fdsan_close_with_tag()`の両方を捕捉し、切断時に接続元とトークンを消去します。

shell起動時だけトークンとbroker FDを環境へ渡し、PATHの先頭にはroot所有ディレクトリへコピーした制限付き`su`クライアントを追加します。追加adbdはPID 1を基準にした新しいmount namespaceで動きます。supervisorの起動時に適用したKernelSUのno-new-privileges属性は子孫へ継承されるため、UID 2000の標準shell互換性を保ったまま、`/system/bin/su`などKernelSU側の入口から再昇格できません。brokerは接続元IPがroot許可リストにあること、トークン、UID/GID 2000、SELinux peer context `u:r:shell:s0`、渡された標準入出力FDをすべて確認してから、既にUID 0のbroker子プロセスでrootコマンドを実行します。rootコマンドの子プロセスはPID 1のmountを基準に専用namespaceへ移り、起動元アプリのデータ隔離を引き継ぎません。切り替えに失敗した場合はコマンドを実行しません。検査エラー、短い制御メッセージ、余分なancillary data、タイムアウトは拒否側に倒します。

ADBのRSA challenge/responseは維持しますが、Samsung実機の`u:r:adbd:s0`からclassic `/data/misc/adb/adb_keys`を読む経路は拒否されました。そこで未知キーの認証callbackでは、現在accept中のTailscale IPがアプリのshell許可リストにある場合だけ、そのセッションを承認します。未許可IPは`unauthorized`のままです。公開鍵をシステム鍵ストアやアプリ設定へ保存せず、秘密鍵、公開鍵本文、セッショントークンをログへ出しません。root許可はshell許可の部分集合として別に検査します。

終了はroot所有PID・UID・lock FDを検査し、pidfdで特定したプロセスへSIGTERMを送ります。3秒で終了しない場合だけ同じpidfdへSIGKILLを送ります。PID再利用による別プロセスの停止を防ぎます。追加adbdは親死亡時にSIGKILLされ、UID降格後にも親死亡通知を設定し直します。3回の短時間連続異常終了で停止し、60秒以上安定した後は失敗回数をリセットします。VPNインターフェース以外への接続はソケット自身の拘束で拒否します。

回線復旧の期限・イベント集約・再試行・クラッシュ判定は `native/network_recovery.h` に分離しています。netlinkは250 msで集約し、確認は最短1秒間隔、通常は15秒の絶対期限で実行します。イベントやADB制御メッセージの連続受信で確認期限を延ばしません。VPN検査は固定argvで実行し、出力・実行時間・全体の検査予算に上限を設けます。一時的なbind失敗は1、2、4、8、15秒で再試行し、権限エラーやポート競合は停止します。VPN所有者・アドレス・インターフェース・network handleが変わらない回線変更ではbackendを維持します。再作成時はpeer、トークン、FDをまとめて破棄します。policyもprivate作業ディレクトリへコピーし、APK更新で古いnativeLibraryDirが消えても再起動できるようにしています。

受け付けたADB TCPにはkeepaliveと45秒のTCP_USER_TIMEOUTを設定し、通信断で単一transport枠が永久に残らないようにしています。`--network-check <user>` は実際のTailscale所有UIDまで照合する読み取り専用操作です。Java側のroot呼び出しも非同期読み取りとタイムアウトを分離し、標準出力待ちで処理中状態が固定されることを防ぎます。

PC連携のプロトコルと境界は[通信復旧の設計](network-recovery.md)を参照してください。

## 対応したバイナリ

SM-S948Q / Android 16、ビルド `BP4A.251205.006.S948QOPS1AZHL`。

adbd SHA-256：`459d1f67760789bdf8eb5a7a5c1bbe409d5dd3837d408ce4d7d14e496fcb8cbb`。

`/system/lib64/libadbd_auth.so` SHA-256：`d078a2db5dcfafa23c2fa6629b28fe9c673ec57b103fb396e7b8f16763329a2f`。

未知のadbdは安全のため拒否します。OS更新への対応には、標準の認証・権限降格・acceptfdの動作を新しいバイナリで確認してから基準を更新してください。ハッシュだけを増やして対応済みと扱わないでください。

Android 17 / ADB Wi-Fi 2.0世代の互換処理と検証用ビルドは[17の互換性記録](compatibility/android17.md)へ分離しています。通常ビルドのAndroid 16許可条件を広げる方式ではありません。`-PtailadbAndroid17Validation=true`は実機試験用であり、通常配布には使用しません。

root境界はKernelSU `samsung-v1.0:KernelSU` version code 32664で検証しています。`su --ksu-no-new-privs`を提供しないroot実装では安全な開始条件を満たさないため対応外です。

## 参照した実装

- [AOSP adbd main.cpp / Android 16](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android16-release/daemon/main.cpp)
- [AOSP socket_spec.cpp / acceptfd](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android16-release/socket_spec.cpp)
- [AOSP shell_service.cpp / shell process](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android16-release/daemon/shell_service.cpp)
- [AOSP libadbd_auth](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android16-release/libs/adbd_auth/adbd_auth.cpp)
- [Tailscale AndroidのVPN実装](https://github.com/tailscale/tailscale-android/blob/main/libtailscale/net.go)

公開可能なファームウェア情報と再監査項目は[`research/sm-s948q-android16-oneui8.md`](../research/sm-s948q-android16-oneui8.md)に保存します。抽出した端末バイナリ、個人ログ、実際のIP・tailnet名、スクリーンショットは`artifacts/`だけに置き、Gitへ追加しません。依存ライセンスの一覧と実際のruntime依存が異なる場合はビルドを停止します。
