# Android 16の基準

基準コミットは`a6dce04e7a724ad9d74feac1e7cd4a0cc9185861`です。既存のadbd／認証ライブラリのSHA-256ペアを`native/profiles/android16.h`に分離しました。ファイルパス、ペア一致の条件、VPNの所有者確認、資格情報の降格、ホスト別許可とroot brokerの検査は変えていません。

元のnativeソースのSHA-256は`android16-source.json`に記録しています。
