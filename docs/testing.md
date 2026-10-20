# 検証記録と手順

## 保存した実機基準（2026-10-17）

| 項目 | 検証時の値 |
| --- | --- |
| 端末 | Galaxy S26 Ultra / SM-S948Q |
| OS / API | Android 16 / 36 |
| One UI | 8.0 (`ro.build.version.oneui=80500`) |
| ビルド | `BP4A.251205.006.S948QOPS1AZHL` |
| セキュリティパッチ | 2026-08-05 |
| Tailscale | `1.104.1-t7f4efe814-g6d75bd403` / code `298567190` |
| Tail ADB | 0.2.1 / code 3 / release / debuggable=false |
| adbd SHA-256 | `459d1f67760789bdf8eb5a7a5c1bbe409d5dd3837d408ce4d7d14e496fcb8cbb` |
| libadbd_auth SHA-256 | `d078a2db5dcfafa23c2fa6629b28fe9c673ec57b103fb396e7b8f16763329a2f` |
| KernelSU | `samsung-v1.0:KernelSU` / code `32664` |

個人の端末シリアル、tailnet情報、秘密鍵、ADB公開鍵本文は公開記録へ含めません。詳しい基準は[実機基準](../research/sm-s948q-android16-oneui8.md)に保存しています。

## 0.2.1で確認済み

- release APKを既存版へ上書きインストールし、設定を維持して起動。
- 専用listenerがTailscale IPと`tun1`だけにbindされていることを`ss -ltnp`で確認。
- 未許可のTailscaleホストは`unauthorized`となり、shellを実行できない。
- 接続元IPがアプリに即時表示され、「shell許可待ち」と判別できる。
- shellトグルをオンにした後、`id`がUID/GID 2000、`id -Z`が`u:r:shell:s0`。
- rootトグルがオフの状態では同梱`su`が拒否する。
- rootトグルがオフの状態で`/system/bin/su -c id`を直接実行してもUID/GID 2000のままで、KernelSUによる再昇格が起きない。
- rootトグルをオンにした後、`su -c id`がUID/GID 0、`u:r:ksu:s0`。
- rootだけをオフにするとshell接続を維持したまま`su`が拒否される。
- shellをオフにするとroot設定も失効し、次の接続が`unauthorized`になる。
- 接続タブと設定タブの許可済みホスト一覧から、同じshell/root権限をトグル操作できる。
- 壊れた「root許可のみ」の引数をnative supervisorが`ERROR_ARGUMENT`で拒否。
- 切断時にpeer状態とセッショントークンを消去。
- 端末のLANアドレスの同ポートは接続拒否。
- 現行MacのADBキーがAndroidのclassic `adb_keys`にない状態でも、アプリでshell許可した接続だけが成立。
- shell許可後にADB syncのpush/pull、`pm`、`settings`がUID 2000の標準shell権限で動作。
- root supervisorと追加adbdをアプリ内操作で停止・再開。
- 常駐通知なしでlistenerがアプリプロセスとは独立して継続。
- Material 3の下部ナビゲーション、トグル、許可済みホスト一覧、日本語表示、ステータスバー余白を実機確認。
- `clean lintRelease assembleRelease`が成功。release署名、`debuggable=false`を確認。

以前の実装で、Androidのワイヤレスデバッグがオフでも動作すること、Wi-Fiを切ったモバイル回線上のTailscaleでもshell接続できることを確認済みです。0.2.1でネットワークbind方式は維持されていますが、モバイル回線の再試験は今回行っていません。

## 未検証

Android 17用のコード・静的照合・ホスト統合試験は[17の互換性記録](compatibility/android17.md)を参照してください。17実機ではMainline ADBと17認証ライブラリの明示ペアを確認しました。ROM内factory本体は未検証です。Android 16の過去の実機結果、17実機の確認範囲、ホスト試験を区別しています。

- 端末の通常再起動またはソフトリブート後の自動開始。端末を無断で再起動しないため未実施。
- rootが一時的に完全消失した状態での自動起動UI。
- 別のTailscaleホストからの同時競合接続。
- 長時間Doze、全ADBサブコマンド、他機種、OS更新後のバイナリ。

## 再現手順

1. 端末とPCをTailscaleへ接続し、アプリからADBを開始する。
2. 表示されたTailscale IPへ`adb connect`し、未許可表示とアプリ上の接続元IPを確認する。
3. shellトグルをオンにして再接続し、`adb shell 'id; id -Z'`を確認する。
4. rootトグルがオフのまま`adb shell 'su -c id'`が拒否されることを確認する。
5. rootトグルをオンにし、同じコマンドがUID 0になることを確認する。
6. 設定タブの許可済みホスト一覧でroot、続いてshellをオフにし、段階的な失効を確認する。
7. LAN IPの同じポートへ接続できないことを確認する。
8. アプリ内の停止で専用listenerだけが終了することを確認する。

```sh
adb logcat -s TailAdbPolicy:I TailAdbSupervisor:I '*:S'
adb shell su -c 'ss -ltnp'
```

## 復旧

通常はアプリ内の「停止」を使います。root supervisorはアプリプロセスから独立しているため、アプリの強制停止だけでは終了しません。他のadbdを巻き込む`killall adbd`や、システムADBプロパティの変更は避けてください。

停止後にアプリをアンインストールできます。作業用コピーは`/data/local/tmp/net.ryuya_dev.tailadb`内だけに置き、system/vendor/product/bootやAndroidのADB鍵を変更しません。
