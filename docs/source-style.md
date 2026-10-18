# ソースの書式

自作のJava、C、Gradle Kotlin DSL、XML、シェル、CI設定を読みやすい書式に統一しています。接続許可、権限境界、通信仕様、設定キー、翻訳文は書式変更の対象に含めません。

| 対象 | 規則と使用したツール |
| --- | --- |
| Java | Google Java Style、スペース2個。google-java-format 1.24.0 |
| C | スペース2個、原則100桁。clang-format 19.1.7とルートの`.clang-format` |
| Gradle Kotlin DSL | スペース4個。ktfmt 0.54の`--kotlinlang-style` |
| XML | スペース2個。Manifestと画像定義は属性を改行。翻訳の各値は本文を変えず1行に保持 |
| シェル・YAML | スペース2個。Prettier 3.6.2、シェルはprettier-plugin-sh 0.18.0 |

基本設定は`.editorconfig`を参照してください。Javaでは`--skip-sorting-imports --skip-removing-unused-imports --skip-reflowing-long-strings`、ktfmtでは`--do-not-remove-unused-imports`を使っています。XMLは`@prettier/plugin-xml` 3.4.2の`xmlWhitespaceSensitivity=preserve`で整形し、翻訳ファイルでは本文を保持して各項目を並べます。シェルの長いコンパイルコマンドは継続行へ分けています。

生成されたGradle Wrapper、バイナリ、第三者ライセンス本文、署名ファイル、実機の調査資料は整形しません。フォーマッターはローカルの一時ディレクトリで実行し、APKへ含めません。

## 変更の確認

書式だけを変えるときは、Java・Cのトークン、XMLの要素・属性・文字列、Gradleの設定値・依存関係を比較します。native側は同じNDK・オプションで再ビルドした両ABIの出力も照合します。releaseビルド、release Lint、シェル構文検査を実行し、署名済みAPKだけを実機へ上書きします。

機能変更は書式整理とは分け、対象の動作とセキュリティ境界を検証してください。OSやSystemUIの再起動は書式整理の検証に不要です。

## 初回整理の検証

2026-10-19の整形では、Java 5ファイル、C 3ファイル、Gradle Kotlin DSL 3ファイル、XML 11ファイル、シェルとCI設定を確認しました。Java・Cのトークン、Gradleの設定と文字列、XMLの属性と本文を比較し、arm64-v8a／x86_64のnative出力6個は変更前とバイト単位で一致しました。

署名済みreleaseビルド、release Lint、シェル構文検査とフォーマッターの再実行確認が通過しています。実行可能な既存ユニットテストはありません。生成コード、Gradle Wrapper、第三者ライセンスとバイナリには手を加えていません。
