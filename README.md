# Tail ADB

Tail ADBは、root化したAndroid端末にTailscale経由だけで接続できる、独立したADB待受を作るアプリです。Wi-Fiとモバイルデータのどちらでも利用でき、Android設定の「ワイヤレスデバッグ」をオフにしても動作します。

待受は検証済みのTailscale VPNインターフェースと端末のTailscale IPv4へ固定します。LAN側には公開せず、Android標準や他アプリのADB待受も変更しません。

## 主な機能

- アプリからTailscale専用ADBを開始・停止
- 接続先コマンドの表示とコピー
- 初回接続ホストを画面に表示し、ADB shellの許可をトグルで管理
- すべてのADBセッションをAndroidのUID 2000 `shell`で開始
- 接続元Tailscale IPごとに`su`によるroot昇格を別のトグルで許可
- 設定画面で許可済みホストとshell/root権限を一覧管理
- 使用するAndroidユーザー／仕事用プロファイルの選択
- Tailscale候補が複数ある場合の候補表示
- Tailscale切断時の安全な停止と、再接続時の待受再作成
- 通常起動・ソフトリブート後の自動開始（オン・オフ可能）
- 常駐通知を使わないバックグラウンド動作
- Material 3 UI、日本語・英語、OS言語への追従、アプリ単独の言語選択

## 対応環境

実機で確認した環境はGalaxy S26 Ultra `SM-S948Q`のAndroid 16 / One UI 8.0（`BP4A.251205.006.S948QOPS1AZHL`）とAndroid 17 / One UI 9（`CP2A.260605.016.S948QOPU1BZI9`、検証済みMainline ADB搭載）です。OS内の`adbd`と認証ライブラリが検証済みバイナリと一致しない場合は、安全のため開始しません。

必要なもの：

- 対応端末で動作し、`--ksu-no-new-privs`を提供するKernelSU環境（検証版はversion code 32664）
- 端末と接続元PCにインストール済みのTailscale
- 同じtailnet内で端末へ到達できるPC
- PC側のAndroid Platform Tools（`adb`）

Xposed、LSPosed、Vectorのスコープ設定は不要です。Tail ADBはroot化、ブートローダー解除、Tailscaleログインを代行しません。

## インストール

[GitHub Releases](https://github.com/ryuya0124/tail-adb/releases)から最新のrelease APKをダウンロードしてインストールします。配布APKは専用のリリース鍵で署名しています。

```sh
adb install -r TailAdb-0.2.2.apk
```

初回起動時にroot権限を自動で要求します。拒否されている場合は画面にエラーを表示するため、root管理アプリでTail ADBを許可してください。

## 使い方

Android 17でもTailscale専用の接続方式を使えます。Android 16の対応も維持しています。17のADB本体には複数の版があるため、動作確認した本体・認証ライブラリの組み合わせだけを許可します。[対応状況](docs/compatibility/android17.md)

1. 端末とPCをTailscaleへ接続します。
2. Tail ADBを開き、必要なら設定からTailscaleを動かしているAndroidユーザーを選びます。
3. 「ADBを開始」を押します。ポートの初期値は5555です。
4. 表示されたコマンドをPCで実行します。

```sh
adb connect 100.64.0.10:5555
```

アドレスは例です。実際のアプリ表示を使ってください。

初めての接続元は`unauthorized`になり、アプリにTailscale IPが「shell許可待ち」と表示されます。「ADB shell接続を許可」をオンにすると専用待受が安全に起動し直され、接続できるようになります。接続直後は必ず次の権限です。

```text
uid=2000(shell) ... context=u:r:shell:s0
```

rootが必要なホストだけ「root昇格を許可」をオンにし、ADB shellで`su -c <command>`を実行します。rootをオフにしてもshell許可は維持されます。shellをオフにするとroot許可も同時に失効します。保存した許可は設定タブの「許可済みホスト」から確認・変更できます。

アプリを閉じても専用待受は続きます。常駐通知は表示しません。終了はアプリ内の「停止」を使用します。「自動起動」をオンにすると、Androidの通常起動またはソフトリブート完了後に開始を試みます。rootが使えない場合は安全に停止し、要求や再試行を繰り返しません。

## セキュリティ上の境界

- 待受ソケットはTailscaleのインターフェース、ネットワーク、IPv4へ三重に固定します。
- 未許可IPはADB shellへ入れません。shell許可済みでも、root許可がなければ同梱`su`は拒否され、KernelSUの`su`を絶対パスで実行してもUID 2000から昇格しません。
- 同時ADBトランスポートを1つに制限し、接続元IPとroot認可セッションの取り違えを防ぎます。
- OSのADBキー、システムプロパティ、system/vendor/product/bootパーティションは変更しません。
- 設定やOS実装を安全に確認できない場合はfail-closedで停止します。
- Tailscale側のACLと端末のroot管理環境も信頼境界に含まれます。

制約と実機検証範囲は[検証記録](docs/testing.md)、脅威モデルと修正内容は[セキュリティ監査](docs/security-audit.md)に記録しています。

## ソースとライセンス

コードは[MIT License](LICENSE)です。依存ライブラリのライセンスはアプリ内の「ライセンス」と[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)で確認できます。Tailscale社の公式アプリではありません。

ビルドと内部構造は[開発者向けガイド](docs/development.md)、検証対象ファームウェアは[実機基準](research/sm-s948q-android16-oneui8.md)を参照してください。

自作ソースの書式と確認方法は[ソースの書式](docs/source-style.md)に記載しています。
