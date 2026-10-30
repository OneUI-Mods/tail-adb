# Kotlinへの移行（2026-10-30）

Android側の実装、JVMテスト、実機用テストコードをKotlinへ移しました。ビルドはAndroid Gradle Pluginの組み込みKotlin / JVM 17を使用します。既存のパッケージ名、設定キー、provider authority、module entry、署名の識別は引き継ぎます。SDK・対象OS・機能の対応範囲を、この移行だけで拡大したものではありません。

C++・Goのソースは変更していません。JNIを持つアプリは、移行前のJava宣言とコンパイル後のクラスからメソッド名・JVM descriptor・static指定を比較しています。

## 動作の維持

- Android 16と17の既存の分岐・検証済み契約を維持します。`docs/compatibility/android16-source.json`がある場合、そのJavaパスとハッシュは過去の固定コミットの記録です。現在のKotlinソースへ書き換えません。
- Javaの数値変換、不変コレクション、nullを受ける復旧経路、recordのコンストラクタ補正・文字列表現を維持します。永続化した設定や確認用識別値を別の形式に変更しません。
- 配布用APKへ検証用Activityや実機テストを含めません。デスクトップ専用のDEX調査ツールはAndroidアプリの実装対象に含めません。

## 確認

```sh
./gradlew :app:compileReleaseKotlin :app:testDebugUnitTest :app:lintRelease :app:assembleRelease
```

既存JVMテスト0件とrelease lintを確認しています。実機用コードはコンパイルの確認と、端末上での実行を区別します。この初期ビルド確認の段階では実機操作は未実施でした。後続の実機確認と区別し、最新の範囲は下記を参照してください。

Kotlinの整形は`ktfmt --kotlinlang-style`を使用します。生成コードを更新した場合も整形とビルドを確認してください。配布時は既存のリリース手順を使い、同じ鍵で署名したrelease variantを検証します。移行ブランチのビルド成果物を、そのまま公開リリースとして扱わないでください。

## エミュレータによる再調査（2026-10-30）

Google APIsのarm64エミュレータ、Android 16（API 36）とAndroid 17（API 37）で、rootを使わずに確認しました。このエミュレータ確認段階では、実機と既存のADB接続は操作していません。

- 設定・ライセンスなど、このアプリに存在する通常Activityの起動、タブの切り替え、Activity再生成を確認。
- 通常表示、ダーク表示＋文字サイズ150%、release variantでのダーク表示＋文字サイズ200%を確認。ライセンス一覧があるアプリでは検索・一覧の再配置・詳細への移動も実行。
- release variantの試験にはローカル専用の試験署名コピーを使用。配布用の署名や公開APKとは区別しています。検証用runnerは別APKです。
- 移行前コミットからJava版を別worktreeでビルドし、同じエミュレータの通常表示とJVMテストで比較。

このエミュレータ試験の結果は、rootなしでアプリ側の画面と設定が動くことを確認したものです。Samsung SystemUI／One UIホーム／Home Up／Camera／Camera Assistant、Vectorのフック実行、root権限、実際の録画・通話音声・物理デバイスの経路は検証していません。Kotlin化前と全機能が同一と断定するものではありません。

## 解除後のAndroid 17実機確認（2026-10-30）

継続使用中と同じ鍵の署名済みrelease版を上書きし、メイン画面が前面に表示されることをUIツリーで確認しました。ライセンス一覧の表示も確認しています。機能スイッチの変更を目的としたON／OFF操作はせず、全アプリの設定を確認前後で退避・照合しました。

Tailscale経由の既存接続から端末のシリアル応答も確認済みです。接続を所有する別ADB serverの停止や、通信断を伴うネットワーク切り替え試験は行っていません。

今回のGUI確認中はOS・system_server・SystemUIを再起動せず、録画開始・音声再生も行っていません。初期のビルド確認や先行するSystemUIの読み込み直しとは別の確認段階です。画面が表示できることだけで全機能を確認済みとは扱いません。Android 16はエミュレータと既存契約までで、Android 16実機の再試験は行っていません。
