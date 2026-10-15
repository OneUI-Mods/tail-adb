# 検証記録と手順

## 保存した実機基準（2026-10-16）

| 項目 | 検証時の値 |
| --- | --- |
| 端末 | Galaxy S26 Ultra / SM-S948Q |
| OS / API | Android 16 / 36 |
| One UIプロパティ | `ro.build.version.oneui=80500` |
| ビルド | `BP4A.251205.006.S948QOPS1AZHL` |
| セキュリティパッチ | 2026-08-05 |
| Tailscale | `1.104.1-t7f4efe814-g6d75bd403` / code `298567190` |
| Tailscaleの場所 | 仕事用プロファイル。実際の所有UIDを検査して接続 |
| Tail ADB | 0.1.0 / code 1 / release / debuggable=false |
| APK SHA-256 | `f4a4367f493a0bacad38e8649ff6bd445b96fc66827fd797c3c65566d2b0965a` |

端末側のadbdハッシュと配布署名は[開発者向けガイド](development.md)に保存しています。OS更新でこの環境が失われても、今回の確認基準を参照できます。個人のIP、端末シリアル、tailnet、接続ログは公開していません。

## 確認済み

- SM-S948Q / Android 16の実機上で、ADB shellの既存su権限を使った独立backendを起動・停止。
- Tailscaleの実インターフェース・アドレスへ `SO_BINDTODEVICE` で拘束されていることを確認。
- 別namespaceにいる追加adbdへMacからTailscale経由で接続し、UID 2000のshellで操作できた。
- 同じ端末のLANアドレスの同ポートは接続拒否。最終release APKでも再確認。
- 最終release APKからの専用接続で、作成した試験ファイルをADB pullし、内容を照合して削除。
- 認証情報を送らない生のADB接続に対しAUTH要求が返り、自動許可されないことを確認。最終release APKでも再確認。
- 子shellに追加待受のソケットFDが継承されないことを確認。
- supervisorへの停止操作で待受が閉じることを確認。
- ユーザーがroot管理側で許可した後、インストールしたreleaseアプリの前景サービスから専用待受を開始。
- Androidの `adb_wifi_enabled=0` でも専用待受からUID 2000のshellへ接続できた。
- Wi-Fiを一時的に無効化し、ConnectivityServiceでVPNの下位回線が `CELLULAR|VPN` になったことと、その経路でshell操作が成功したことを確認。
- 試験後はWi-Fiと標準ワイヤレスデバッグの設定を元のオン状態へ復元。
- アプリ内の停止ボタンで専用待受と子adbdが終了することを確認。
- 同じポートですぐに停止・再開し、再接続できることを確認。TCPのTIME_WAITを考慮しSO_REUSEADDRを使用。
- アプリを強制停止すると専用待受と子adbdが消え、標準ADBがそのまま使えることを確認。
- Material 3の設定画面をrelease APKで起動。画面端の余白、日本語、下部ナビゲーションを確認。
- 専用の署名によるreleaseビルド、`debuggable=false`、署名検証、release Lintが成功。

## 未検証の範囲

通知の停止ボタンの実タップ、承認キー削除による切断、未知のOS、複数VPN、長時間のDoze、すべてのADB機能は未検証。既存キーを削除するテストは実施していない。

## 実機確認の手順

1. root管理でTail ADBを許可し、Tailscaleに接続する。
2. アプリの開始ボタンから起動し、常駐通知と待受表示を確認する。
3. Mac等から表示されたアドレスへ `adb connect` し、shell・ファイル転送を確認する。
4. 端末のLANアドレスで同ポートへ接続できないことを確認する。
5. Androidのワイヤレスデバッグ設定を一時的にオフにしても本アプリの接続が残るか確認し、元の設定へ戻す。
6. Wi-Fiを一時的にオフにし、モバイルデータ上のTailscale経由で接続する。元のWi-Fi状態を復元する回復手段を先に用意する。
7. 停止ボタン、通知の停止、アプリの強制停止で接続が終了することを確認する。
8. Tailscale切断・再接続、ポート使用中、root拒否、未知のOSでは待受を勝手に他のインターフェースへ広げないことを確認する。

```sh
adb logcat -s TailAdb:I '*:S'
adb shell su -c 'ss -ltnp'
```

## 復旧

通常はアプリか通知の停止ボタンを使用します。アプリの強制停止でも制御パイプが閉じ、専用backendが終了します。他のadbdを巻き込む `killall adbd` や、システムのADBプロパティ変更はしないでください。

作業用コピーは `/data/local/tmp/net.ryuya_dev.tailadb/adbd` で、通常終了時に削除します。root所有の作業ディレクトリとlockファイルだけが残る場合があります。アプリを停止した後にこのアプリ専用のディレクトリを削除できます。システムファイルや既存のADBキーは削除しません。
