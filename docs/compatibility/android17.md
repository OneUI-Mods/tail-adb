# Android 17の対応準備

- 候補ファームウェア: SM-S948Q / S948QOPU1BZI9。
- 状態: 別プロファイルを用意し、まだ有効化していません。端末へのインストール・backendの置き換えは行いません。
- VPN検出: 接続中のTailscaleへ到達できても、TailADB UIが「Tailscaleの接続待ち」になる状態を調査中です。Android 17のConnectivityService出力と実機のVPN所有者・ユーザー・インターフェースを照合する必要があります。
- daemon: ROMのadbdとlibadbd_authを抽出してABI・ハッシュを記録し、実機で認証、UID 2000、SELinux、隔離、未許可ホストの拒否、明示的root昇格を別々に確認します。ハッシュを追加するだけでは対応済みにしません。
- 16の完全一致ペアと、不明・組み合わせ違い・空のペアを判定するCテストを追加しました。
- 検証用のビルドを作っても、利用者が通信を確保して更新を許可するまでは実機へ反映しません。

## BZI9 ROMの解析（2026-10-20）

SDK 37、One UI 90000、ビルド表示`CP2A.260605.016.S948QOPU1BZI9`を確認しました。圧縮APEXの`original_apex`から`apex_payload.img`を開き、ADB本体を抽出しています。元のROMは変更していません。

| ファイル | SHA-256 |
| --- | --- |
| `adbd` | `d4d3141261f2f6c68d3effcf2c2aaf7a8167a3d882011fe51cd8248b5fa8a501` |
| `libadbd_auth.so` | `46bff7ced0447286293b5597b43a660db0ae3a954dd9a5db9725bf6bb6cb6a6e` |

このペアは16の検証済みペアと異なります。17の定義へ記録しましたが、`verified=false`を維持しています。実機がこのROMと同じファイルを使っている場合、現行版の完全一致検査では起動できません。現在の実機ファイルは取得できていないため、接続障害の確定原因とはしません。

Cテストで16のペアの許可、未知・混在ペアの拒否、記録した17のペアが未許可であることを確認しました。NDKのarm64-v8aとx86_64のnativeビルドも通過しています。APKのインストール、端末backendの交換、設定変更は行っていません。

## 状態表示で見つかった問題

既存の`supervisor.c`では`main()`が`status_path`初期化より前に`ERROR_COMPAT`で終了します。その場合、エラーは標準出力には出ますが永続statusへ書き込まれません。さらに`status_backend()`はPID・lockの現在の所有を確認せず以前のstatus/peerを返します。UIが定期取得すると、起動失敗後に以前の「接続待ち」や接続元へ戻る可能性があります。スクリーンショットの表示と整合する候補ですが、実機ログによる確定はまだです。

修正時は次を分離して検証します。

1. 動作中のbackendを他の開始要求が上書きしないようlockの所有を確認する。
2. 起動失敗の状態を永続化し、終了したbackendの接続待ち・peerを稼働中として返さない。
3. PIDの再利用や、別プロセスのlockを稼働中と誤認しない。
4. 実行中backend、停止後、未対応ペア、権限不足、再接続それぞれでUIが最新状態になることを確認する。

状態表示の修正はまだ実装していません。17の認証ライブラリABI、VPN検出、shell/root分離の確認も残っています。接続先への接続拒否が続いていますが、外出中の通信を守るため端末更新は保留します。

## 認証APIの追加調査

ROMのAArch64 ELFから動的シンボルを取得し、ビルドしたpolicyのexportと照合しました。ADB本体が必要とする認証関数13個のうち、現行policyが介入していないものは`adbd_auth_register_service`と`adbd_auth_unregister_service`です。[android17-auth-symbols.json](android17-auth-symbols.json)にハッシュと結果を保存しています。

[Android 17の認証ヘッダー](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android17-release/libs/adbd_auth/include/adbd_auth.h)では、これらはmDNSサービスの登録・解除APIです。[認証の初期化](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android17-release/daemon/auth.cpp)にはコールバック版1/2/3の分岐があり、現行policyは1を返すため、版3を必ず要求するとは判断していません。[mDNS側](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/android17-release/daemon/mdns.cpp)のフラグとframework接続通知の分岐を含めて確認する必要があります。

シンボルが存在しても、関数引数のABI、コールバックの寿命、実機のフラグ設定までは確認できません。ローカル認証用contextが未介入の実ライブラリへ渡されないことと、LANへサービスを広告しないことを確認するまで、17のハッシュを許可しません。この段階で成功を返すだけのダミー関数を追加してはいません。
