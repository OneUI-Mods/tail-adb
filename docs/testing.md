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

## 0.2.4 通信復旧（2026-10-21）

Android 17 / One UI 9の対応済みSM-S948Qに署名済みrelease APKを上書きしました。以前のAndroid 16許可プロファイル・バイナリ照合条件は変更していません。今回の実機試験は17のみです。

- 選択ユーザーのTailscale診断：実際に接続中の2ユーザーは各ユーザーのUID・VPN・IPでREADY、未接続の別ユーザーはOTHER_USER。設定のユーザー選択は変更せず、Service経由でも選択ユーザーのREADY保存を確認。
- ADBと状態通知TCPの両方がTailscaleアドレス・VPNインターフェースに限定されていることを確認。
- PCの専用foreground ADB serverだけをSIGSTOPして応答不能を再現。約13秒で専用serverを置き換え、ADB echoが復帰。端末側supervisor/adbd、SystemUI、system_server、ホーム、kernel boot IDは不変。
- Wi-FiがOFF、モバイルデータがONの状態から、端末側の独立復元スクリプトを準備してモバイルデータを12秒OFF。状態通知に到達不能な期間はserverを再起動せず、到達復帰後に1回だけPC専用serverを再作成し、開始から約34秒でADB echoが復帰。端末側supervisor/adbdと上記OSプロセスは不変。Wi-Fi OFF・モバイルデータONへ復元し、一時スクリプトも削除。
- APK上書き後もprivateディレクトリ内のpolicyを使用し、パッケージ更新によるnativeLibraryDirの変更で動作中backendが失われないことを確認。
- sanitizer付きnative試験：両OS/ABIプロファイル、認可、状態record、回線イベント集約・再試行・クラッシュ回数、コマンドの時間/出力上限・中断・子回収が成功。
- Linux統合試験：実プロセスの状態/停止/lock所有者、SIGTERMを無視する子へのpidfd停止、認証の許可境界、状態通知TCPの認可・全state、keepalive/TCP timeout、peer・token・FDの破棄が成功。専用のnetworkなし使い捨てコンテナで実施し、既存のVMやコンテナは変更していない。
- Python companion：14試験が成功。分割TCP応答、壊れた状態通知、未許可ホスト、専用子だけの停止、再起動間隔、認証待ち、disconnect停止、初回connect、所有server不在時にclientから自動起動しないことを確認。
- `lintRelease`・`assembleRelease`・署名・16 KiB alignment・SDK 37・非debuggable・検証用Activity非混入を配布手順で確認。

今回未検証：実際のWi-Fiとモバイル回線の往復、長時間圏外/Doze、Tailscaleアプリ自体の再起動、Android 16での今回版の再試験。Wi-Fi往復は接続できるAPを確認できていないため未実施。状態表示のデータ経路は実機確認したが、端末がロック中のため追加カードの目視確認は未実施。自動復帰の所要時間はネットワークや認証の状態によって変わる。

## 0.2.6 端末側の競合接続待機

詳細は[端末側の省電力対策](device-power.md)を参照。Android 17の対応済み実機でMacとWindowsの競合・双方向の引き継ぎ、root、ファイル転送を確認。端末本体とOSプロセスは再起動していない。Android 16の今回版、多数PC、長時間の待機は未確認。
