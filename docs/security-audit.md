# セキュリティ監査

## 対象

2026-10-16に、Tail ADB 0.1.0、Galaxy S26 Ultra SM-S948Q、Android 16、ビルド `BP4A.251205.006.S948QOPS1AZHL`、端末内adbd SHA-256 `459d1f67760789bdf8eb5a7a5c1bbe409d5dd3837d408ce4d7d14e496fcb8cbb` を対象に確認しました。ネットワーク境界、ADB認証、Linux資格情報、SELinux、プロセス・namespace・FD分離、Androidアプリの公開面、ビルド依存関係を調べています。

## 重大な検出事項

0.1.0の追加adbdはUID/GID 2000、effective capabilityなしでしたが、SELinux contextが `u:r:ksu:s0` のままでした。このroot管理用ドメインは端末上でpermissiveに設定されており、認証済みTail ADB shellから `su -c id` と `su 0 id` が追加確認なしにUID 0で成功しました。

影響は、Androidが既に承認したADB秘密鍵を持ち、Tailscale ACL上で端末の待受へ到達できるホストが、アプリ側の許可なしにrootを取得できることです。Tailscale bindとADB RSA認証は外部到達範囲を狭めますが、root昇格の認可にはなりません。0.1.0はこの用途ではfail-openでした。

## 0.2.0の修正設計

- 追加adbdをroot管理の `ksu` ドメインからAndroid標準の `adbd` ドメインへ移し、UID/GID 2000、capabilityなし、`u:r:adbd:s0` を確認してからacceptします。
- AOSP SELinuxポリシーの `adbd` から `shell` への遷移を使い、通常コマンドを `u:r:shell:s0` で実行します。
- 同時ADBトランスポートを1つに限定し、acceptしたTailscale IPv4と256-bitのランダムなセッショントークンを対応付けます。
- mount namespace内で検出した既存 `su` パスを同梱クライアントへ差し替えます。PATH名と絶対パスのどちらでもroot管理側へ直接到達させません。
- root brokerは、アプリの許可IPリスト、現在の接続元IP、セッショントークン、peer UID/GID 2000、SELinux `u:r:shell:s0`、SCM_RIGHTSで受けた3つの標準入出力FDを検査します。
- 制御FDとlistenerの `FD_CLOEXEC` は設定結果を読み戻します。SELinux API、資格情報、capability、親PID、制御メッセージ長、ancillary data、broker timeoutのいずれかが不明なら停止または拒否します。
- 常駐通知付きForeground Serviceを廃止し、root所有supervisorを明示的なstart/stop/status操作で管理します。

## 維持される境界

listenerは検証したTailscale VPNの実所有UID、ネットワークハンドル、UP状態のインターフェース、CGNAT /32を照合し、`SO_BINDTODEVICE`、`android_setsocknetwork()`、IP指定bindをすべて適用します。追加adbdは別network namespaceに入り、作成済みlistenerだけを継承します。Androidの既存ワイヤレスデバッグ、システムプロパティ、system/vendor/product/boot、ADB承認キーは変更しません。

ADB接続ではAndroid既存のRSA承認キーを利用します。root許可リストはその上に加える第2の認可です。ADBキーを持たないホストはshellへ入れず、ADBキーだけを持つ未許可ホストはshellまで、ADBキーと許可Tailscale IPの両方が一致したホストだけが `su` を利用できます。

## 残る制約

- root管理実装そのもの、Tailscaleアプリ・tailnet ACL、既に承認済みのADB秘密鍵が侵害されていないことを前提とします。
- 許可は現在のTailscale IPv4に対して行います。IPの再割当て時は許可リストを見直す必要があります。
- 検証済みadbdの完全一致ハッシュだけを受け付けます。OS更新後は新しい実装でSELinux遷移とshell生成経路を再監査するまで開始しません。
- 0.2.0の修正はreleaseビルドと静的検査を通過していますが、実機結果は[検証記録](testing.md)の必須項目が完了するまで未検証として扱います。
