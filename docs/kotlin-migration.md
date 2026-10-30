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

既存JVMテスト0件とrelease lintを確認しています。実機用コードはコンパイルの確認と、端末上での実行を区別します。この移行版に対する実機の画面操作・音声経路の確認は未実施です。端末・SystemUI・ホーム・音声サービスの再起動は実施していません。

Kotlinの整形は`ktfmt --kotlinlang-style`を使用します。生成コードを更新した場合も整形とビルドを確認してください。配布時は既存のリリース手順を使い、同じ鍵で署名したrelease variantを検証します。移行ブランチのビルド成果物を、そのまま公開リリースとして扱わないでください。

## エミュレータによる再調査（2026-10-30）

Google APIsのarm64エミュレータ、Android 16（API 36）とAndroid 17（API 37）で、rootを使わずに確認しました。実機と既存のADB接続は操作していません。

- 設定・ライセンスなど、このアプリに存在する通常Activityの起動、タブの切り替え、Activity再生成を確認。
- 通常表示、ダーク表示＋文字サイズ150%、release variantでのダーク表示＋文字サイズ200%を確認。ライセンス一覧があるアプリでは検索・一覧の再配置・詳細への移動も実行。
- release variantの試験にはローカル専用の試験署名コピーを使用。配布用の署名や公開APKとは区別しています。検証用runnerは別APKです。
- 移行前コミットからJava版を別worktreeでビルドし、同じエミュレータの通常表示とJVMテストで比較。

この結果は、rootなしでアプリ側の画面と設定が動くことを確認したものです。Samsung SystemUI／One UIホーム／Home Up／Camera／Camera Assistant、Vectorのフック実行、root権限、実際の録画・通話音声・物理デバイスの経路は検証していません。Kotlin化前と全機能が同一と断定するものではありません。
