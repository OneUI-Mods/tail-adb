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
- 回線切り替え・通信復帰時の待受再作成と、失敗時の自動再試行
- 選択したAndroidユーザーでTailscaleに接続しているかを確認
- TCPは通るのにADBが応答しない場合の、任意のPC向け復旧コマンド
- 通常起動・ソフトリブート後の自動開始（オン・オフ可能）
- 常駐通知を使わないバックグラウンド動作
- Material 3 UI、日本語・英語、OS言語への追従、アプリ単独の言語選択

## 対応環境

実機で確認した環境はGalaxy S26 Ultra `SM-S948Q`のAndroid 16 / One UI 8.5（`BP4A.251205.006.S948QOPS1AZHL`）とAndroid 17 / One UI 9（`CP2A.260605.016.S948QOPU1BZI9`、検証済みMainline ADB搭載）です。OS内の`adbd`と認証ライブラリが検証済みバイナリと一致しない場合は、安全のため開始しません。

必要なもの：

- 対応端末で動作し、`--ksu-no-new-privs`を提供するKernelSU環境（検証版はversion code 32664）
- 端末と接続元PCにインストール済みのTailscale
- 同じtailnet内で端末へ到達できるPC
- PC側のAndroid Platform Tools（`adb`）

Xposed、LSPosed、Vectorのスコープ設定は不要です。Tail ADBはroot化、ブートローダー解除、Tailscaleログインを代行しません。

## インストール

[GitHub Releases](https://github.com/OneUI-Mods/tail-adb/releases)から最新のrelease APKをダウンロードしてインストールします。配布APKは専用のリリース鍵で署名しています。

```sh
adb install -r TailAdb-VERSION-release.apk
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

## 接続の確認と自動復旧

接続画面の「選択したユーザーのTailscale接続を確認」で、設定したAndroidユーザーのTailscaleが実際に接続しているかを調べられます。別ユーザーだけの接続、未接続、確認失敗を区別します。確認操作は接続先や許可を変更しません。

回線変更時も同じTailscale接続が有効なら動作中のADBを維持します。VPNが失われた場合は待受を閉じ、接続が戻るまで間隔を空けて再試行します。通信断中に実行していたコマンドの継続は保証しません。

PC側も自動で復旧させる場合は、リリースに添付した `tailadb_watch.py` を保存し、アプリの「PC側の復旧コマンドをコピー」を使います。Python 3.10以降と新しいAndroid Platform Toolsが必要です。

```sh
python3 tailadb_watch.py --device 100.64.0.10:5555
```

先に通常のADBサーバーから同じ端末への接続を切ってください。スクリプトは専用のローカルADBサーバーを起動し、接続用の `adb -P <port> -s <device>` を表示します。端末から読み取った状態とADBの応答を照合し、再接続でも直らない場合だけ専用サーバーを再起動します。通常のADBサーバーは停止しません。終了はCtrl+Cです。サーバーのポートは再起動時に変わります。

状態確認用TCP窓口はADBポートの次の番号です（65535の場合は65534）。Tailscale専用で、shell許可済みホストだけが状態を読み取れます。端末側からPCのADBを直接再起動する機能ではなく、PC上で起動したスクリプトが復旧を実行します。

## 接続と権限

待受はTailscale専用です。接続元ごとのshell許可とroot許可を分けて管理し、未許可の接続元へshellを開きません。Tailscaleの接続・ユーザー・端末側の対応を確認できない場合は停止します。詳しい境界と検証は[開発者向けガイド](docs/development.md)を参照してください。

## ソースとライセンス

コードは[MIT License](LICENSE)です。依存ライブラリのライセンスはアプリ内の「ライセンス」と[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)で確認できます。Tailscale社の公式アプリではありません。

ビルド・内部構造・検証環境は[開発者向けガイド](docs/development.md)を参照してください。

自作ソースの書式と確認方法は[ソースの書式](docs/source-style.md)に記載しています。
