# Kotlinの動作互換性（0.2.5）

接続情報、接続元一覧、保存済みの候補を表示するとき、一部の空白文字や空の文字列を従来どおりに扱うよう修正しました。

Android実装と追加試験はKotlinのまま。TextCompatibilityはKotlinのChar判定とRegexで、改行禁止スペース、空入力、先頭のゼロ幅一致、末尾の空要素を旧仕様へ揃える。Javaファイルへ戻したり、製品からJDKのString.strip/splitへ委譲したりしない。C++・Go、Android16/17アダプター、設定キー、パッケージ、署名は維持する。

本アプリのJVM 5件、release Lint・ビルドが成功。Android16/17の専用エミュレータで、実release APKの65,536文字と48分割ケース、不正な正規表現を確認した。元Javaの試験は元のまま別に適用し、Kotlinへ移した試験と同じ件数へ混ぜない。

全体の調査・結果・限界は[OneUI Mods Managerの調査報告](https://github.com/OneUI-Mods/OneUIModsManager/blob/codex/kotlin-migration-20261030/docs/kotlin-behavior-audit.md)。検証ツールの新規実装もKotlin。参照スナップショットのJavaは比較用であり、アプリへ含めない。

署名済みAPK・ソース一致と実機への適用結果は、配布時の検証記録で別途確認する。全ホストの実動作・全入力やAndroid16のSamsung実機を保証する記録ではない。
