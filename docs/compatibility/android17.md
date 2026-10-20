# Android 17 / ADB Wi-Fi 2.0の互換性

## 対応方針と現在の状態

Tailscale専用の接続をAndroid 17へ対応させます。通常のワイヤレスデバッグ用のQRペアリングやLAN内の自動検出は追加しません。Android 16の認証、許可済みホスト、shell/rootの分離、既存設定を維持します。

17向けの認証互換処理と状態表示の修正を実装しました。2026-10-20にSM-S948Qの実機で、稼働中のMainline ADB本体と17の認証ライブラリの組み合わせを検証しています。この完全一致ペアを通常版で許可します。ROM内のfactory ADB本体は別の候補として残し、実機未検証のため検証用ビルドだけで許可します。

公式の[ADB Wi-Fi 2.0説明](https://developer.android.com/tools/adb#connect-to-a-device-over-wi-fi)はAndroid 17とADB 37.0.0以降を対象にしています。TailADBでは、その世代のadbdを既存のTailscale専用listenerへ接続する互換処理を扱います。

## ROMの基準

SM-S948Q / BZI9、SDK 37、One UI 90000、ビルド表示`CP2A.260605.016.S948QOPU1BZI9`を確認しました。圧縮APEXの`original_apex`から`apex_payload.img`を開いてADB本体を抽出しています。元のROMは変更していません。

| ファイル | SHA-256 |
| --- | --- |
| `adbd` | `d4d3141261f2f6c68d3effcf2c2aaf7a8167a3d882011fe51cd8248b5fa8a501` |
| `libadbd_auth.so` | `46bff7ced0447286293b5597b43a660db0ae3a954dd9a5db9725bf6bb6cb6a6e` |

実機ではfactory APEXではなく、更新されたMainline APEX（versionCode `372038020`）が有効でした。稼働中のADB本体は次のハッシュで、16で監査した本体と同一です。モジュール自体をAndroid 16版と判断しているわけではありません。

| 実機で有効なファイル | SHA-256 | 選択するpolicy ABI |
| --- | --- | --- |
| `adbd` | `459d1f67760789bdf8eb5a7a5c1bbe409d5dd3837d408ce4d7d14e496fcb8cbb` | `android16` |
| `libadbd_auth.so` | `46bff7ced0447286293b5597b43a660db0ae3a954dd9a5db9725bf6bb6cb6a6e` | 同上の明示ペア |

`android17_legacy_adbd.h`へこのペアを独立して記録しました。ADB本体だけ、OSの表示だけで互換性を推定せず、認証ライブラリも一致したときに16用の処理を選びます。未知・空・登録していない混在ペアは拒否します。factory 17のプロファイルと認証API対応は削除していません。16の元ソースと許可ペアは[android16.md](android16.md)を参照してください。

## 認証APIと接続管理

[Android 17の認証初期化](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android17-release/daemon/auth.cpp)は、`adbd_auth_get_max_version()`が1ならV1のコールバック構造を使います。TailADBは両OSで1を返します。V2/V3の開始・停止通知を受けるために、標準Wi-Fi TLSサーバーやsystem_serverとの接続を作る必要はありません。

supervisorはハッシュから選んだ`android16`または`android17`のABI名を追加adbdへ渡します。policyは不明なABI名を拒否し、環境変数を消去します。V1の構造体配置、必須コールバック、許可済み接続元と有効なセッションによる承認を検査します。

ROMのELFでADB本体が必要とする認証関数は13個です。以前のpolicyにはなかった`adbd_auth_register_service`と`adbd_auth_unregister_service`を追加しました。これにより、ローカルcontextが実ライブラリのC++オブジェクトとして扱われる経路を防ぎます。名称不正はAPI定義に合わせたエラーを返し、Tailscale専用listenerで想定外のサービス登録へ入った場合は明示的に停止します。成功を返すだけのダミー登録ではありません。

ヘッダー・引数型は[17の認証API](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android17-release/libs/adbd_auth/include/adbd_auth.h)と照合しました。Android 17の[Wi-Fi側](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android17-release/daemon/adb_wifi.cpp)、[mDNS側](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android17-release/daemon/mdns.cpp)、`acceptfd`、権限降格の呼び出しも読みました。新旧のlifecycleフラグ経路があるため、実機ではフラグと従来のmDNS経路も確認します。

静的なシンボル照合は次で再現できます。必要な13関数がすべて自作policyのexportにあることを確認しました。名前の一致だけで引数ABIや実行時の安全性が保証されるわけではありません。

```sh
python3 tools/check-auth-abi.py \
  --readelf /path/to/llvm-readelf \
  --adbd /path/to/audited/adbd \
  --policy app/build/generated/nativeLibs/arm64-v8a/libtailadb_policy.so \
  --reference docs/compatibility/android17-auth-symbols.json \
  --output /path/to/auth-coverage.json
```

`android17-auth-symbols.json`の未介入関数の一覧は、修正前の調査基準です。現在の検査結果は実行時の出力先へ保存します。

## 起動失敗後の古い表示を修正

従来は、互換性エラーが永続statusへ書き込まれる前に終了し、定期取得が古い「接続待ち」と接続元を再表示する可能性がありました。次のように変更しています。

- lockの所有を確保した開始要求だけがstatus/peerを書き換えます。競合する開始要求は稼働中の状態を壊しません。
- `ERROR_COMPAT`も永続化し、Java側で汎用的な起動エラーへ上書きしません。
- 「接続待ち」「待受中」を返すには、root PID、実際に開いているlockのinode、排他lock、プロセスの生存を確認します。
- 接続元を返すのは実際に待受中のときだけです。VPN接続待ちや停止後の古いPID・接続元は表示しません。終了理由のエラーは保持します。シンボリックリンクや不正な状態レコードは読みません。

## ビルドと検証

通常版と17検証版でarm64-v8a・x86_64のnativeコンパイル、release Lintとreleaseビルドを確認しました。検証版は`versionName`に`-android17-validation`が付きます。debug Activityやdebuggableを有効化する方式ではありません。

```sh
python3 tools/test-native.py
./gradlew :app:lintRelease :app:assembleRelease
./gradlew -PtailadbAndroid17Validation=true :app:lintRelease :app:assembleRelease
```

Gradleのnativeタスクはこの設定を入力として追跡し、ビルド方式を切り替えた際に古いnative成果物を流用しません。通常ビルドへ戻す際は、プロパティを付けずに再ビルドします。検証版を通常のGitHub Releaseや利用者向け配布物へ混ぜないでください。

ASan/UBSanを使ったportable Cテスト4実行（ペア判定の通常/検証版、認証ABI、状態レコード）に加え、Linux上で実際の`policy.c`と`supervisor.c`を組み込む統合テスト2本が通過しています。

- 認証: 両ABIのV1初期化、未許可ホスト拒否、有効なセッションと許可ホストがそろった場合だけの承認、失効後の拒否、追加APIの停止と不明ABIの拒否。
- 状態: 実際の起動失敗の永続化、lock所有者の確認、競合開始、無関係なPID、終了後の状態・peer除外、symlink拒否。

Linuxの統合試験は、Androidでも実運用backendのあるホストでもなく、使い捨てのLinux環境で行ってください。Androidのヘッダー代用品は`tests/host_include`にあり、APKのコンパイルには含まれません。

```sh
# Disposable Linux only; requires root within that environment.
bash tools/test-linux-integration.sh
```

## Android 17実機の確認（2026-10-20）

署名済みrelease検証版で確認後、通常版0.2.2（code 4）を上書きしました。通常版でも既存設定を維持し、shell・root接続が復帰することを確認しました。専用backendだけを停止・再開しました。端末、SystemUI、One UIホームは再起動していません。

- 実際のADB本体と認証ライブラリのSHA-256が上記ペアと一致。
- Tailscale専用IPv4・VPNインターフェースだけで待受。LANアドレスの同ポートへは接続できない。
- ADB shellはUID/GID 2000、SELinux `u:r:shell:s0`、SELinux全体はEnforcing。
- root許可時は同梱`su`がUID 0、`u:r:ksu:s0`。root未許可で開始したbackendでは同梱`su`が拒否し、絶対パスの`/system/bin/su`でもUID 2000のまま。
- shell未許可ではADB接続がunauthorizedとなり、アプリへ承認待ちの接続元を通知。
- 停止後のstatusはSTOPPEDで、古いpeerを再表示しない。許可済み設定で再開するとshell・root接続が復帰。
- ADB syncでpushしたバイト列を取得し、一致を確認。
- 標準ADBのUSB・TLS接続と共存し、システムADBのTCPポート設定は変更なし。

一時的なテスト用権限はbackendの引数にだけ渡し、保存されたshell/rootホスト設定を変更せず復帰しました。生の接続元アドレス、シリアル、設定バックアップ、セッション情報はGitへ含めません。

## rootコマンドのmount namespace（2026-10-20）

Android 17実機で、アプリから起動したbrokerがアプリのデータ隔離mountを継承し、許可済みrootコマンドでも他アプリのデータが見えない問題を確認しました。認可を済ませたrootコマンドの子プロセスだけがPID 1のmount namespaceを基準に専用namespaceを作るよう変更しました。broker、UID 2000のshell、ほかのプロセスのmountは変更しません。切り替えが失敗した場合はコマンドを実行せず終了します。

修正版の署名済み通常releaseを実機へ更新し、rootで他アプリのデータディレクトリを参照できること、専用mount namespaceであることを確認しました。端末の再起動は不要です。

## 残る確認

- ROM内のfactory 17本体（`d4d314…`）での認証・隔離・lifecycle・mDNS動作。現実機はこの本体で動作していないため未検証で、通常版の許可対象にはしません。
- Android 16実機での再試験。従来の完全一致ペアと処理は維持しています。
- VPN変更、別プロファイル、別ホスト同時競合、長時間Doze、通常起動・ソフトリブート後の復帰。

システム再起動を伴う確認は、再起動不要な試験を終えてから最後に行います。
