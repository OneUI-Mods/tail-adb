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
