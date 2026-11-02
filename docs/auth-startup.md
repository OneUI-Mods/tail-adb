# 登録済み鍵の起動時読み込み

## 修正する挙動

0.2.6では、新しいホストADBサーバーから接続すると`failed to authenticate`が返り、その直後には同じtransportが`device`になっていた。shell、許可済みroot、ファイル転送はその後に動作した。

Galaxy Android 17のログでは、鍵の列挙callbackが走る時点でadbdはUID 2000 / `u:r:adbd:s0`となっており、`/data/misc/adb`を開く際に`EACCES`になっていた。そのため登録済みの公開鍵を使った署名検証ができず、後段のTailscale送信元許可へ回っていた。接続元Macの公開鍵はAndroidへ登録済みであり、ローカルの秘密鍵との対応も一致した。

## 実装

専用adbdのpolicy constructorで、KernelSUから引き継いだrootのコンテキストがある間に公開鍵を検証・読み込みする。その後、従来どおりadbdコンテキストとshell権限へ移る。認証callbackはプロセス内の読み取り専用スナップショットを参照し、特権ファイルを開き直さない。

既存のファイル/ディレクトリ所有者・モード・上限・安定読み取り・鍵行形式の検査を保つ。読み込み後に特権FDを残さず、秘密鍵を扱わない。SELinux・ファイル権限・Androidの登録鍵・認証設定は変更しない。接続元の許可、セッションtokenとroot許可は引き続き独立して検証する。

supervisorは起動前から鍵ディレクトリを監視し、鍵ファイルの変更・削除・置き換えで既存backendとセッションを破棄して作り直す。鍵の削除が次のbackendへ持ち越されることはない。未登録の公開鍵は既存の送信元承認経路を使い、署名検証を成功したことにはしない。

## 検証

- sanitizer付きportable native試験、Python 14件、JVM 5件、releaseビルドとLint。
- disposable Linuxの実policy関数で2鍵を読み、UID 2000へ落として鍵ディレクトリの読み取りを拒否。従来の読み取り関数は失敗し、修正版のcallbackはAndroid 16/17の両ABIで2鍵を返すことを確認。
- 鍵の削除、新しいbackendでの再読み込み、symlinkと不正な所有者の拒否を確認。既存の送信元・token・mDNS・接続待機の統合試験も維持する。
- 実機では署名済みreleaseを使用し、OSやSystemUIの再起動とGUI操作をせずに、新規ホストADBサーバーからの接続・root・転送・接続枠の引き継ぎを比較する。

実機試験はAndroid 17の対応済みGalaxyに限る。Android 16はABIとホスト試験での維持を確認し、今回のSamsung Android 16実機試験は行わない。未登録鍵、異なるroot実装、未知のadbd、回線切替、長時間接続まで確認済みとは扱わない。

認証callbackの契約は[AOSPのadbd_auth.h](https://android.googlesource.com/platform/frameworks/native/+/94ae13178781a14867071636fded56c907a9db8b/libs/adbd_auth/include/adbd_auth.h)、[daemon/auth.cpp](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/daemon/auth.cpp)を参照する。

## Android 17実機結果（2026-11-02）

署名済み0.2.7を上書きし、専用待受だけを再開した。新しいADBサーバー2つの初回接続、A→B、B→Aの全3回で認証失敗表示がなくなった。実機ログでも登録済み鍵による署名承認を確認し、後段の送信元許可へのフォールバックと鍵の読み取りエラーは発生しなかった。ログの初期化も権限変更後に行い、認証中の診断出力を維持する。

shell UID2000、許可済みroot UID0、262,144バイトのpush/pullとSHA-256一致、選択ユーザーのTailscale所有者を確認。待機中も既存接続のecho/rootが継続した。同じMacの2サーバーであり、今回版の別OS・別IP試験とは区別する。

保存設定、boot ID、system_server/SystemUI PID、SELinux状態を維持した。GUI・音声・OS再起動は使わず、専用サーバー・一時ファイル・共有leaseを返却した。実機のAPKハッシュと配布APKを照合した。
