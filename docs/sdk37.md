# SDK 37移行（2026-10-20）

compileSdkとtargetSdkを37へ更新しました。ビルド環境はJDK 21、AGP 9.3.2、Gradle 9.6.1、Android SDK Platform 37.0、Build Tools 37.0.0です。CIのSDK指定も揃えています。最低対応APIと16/17別のフック定義・実機契約は変更していません。SDK更新だけで未対応ファームウェアを許可しません。

[Android 17のSDK設定](https://developer.android.com/about/versions/17/setup-sdk)、[target 37の動作変更](https://developer.android.com/about/versions/17/behavior-changes-17)、[全アプリ向け変更](https://developer.android.com/about/versions/17/behavior-changes-all)とソースを照合しました。独自のMessageQueue内部アクセス、static finalへの書き込み、旧BAL定数、書き込み可能ファイルに対するSystem.loadは見つかりませんでした。フックは読み込み先プロセスの権限とtargetで動作するため、設定アプリのtarget更新とホストの互換性確認は別に扱います。

4本のreleaseビルドとrelease Lint、既存JVM試験を実行しました。Android 16の実機は今回再試験していません。Android 17実機の更新・起動結果は追記します。

TailADBのアプリUIDはソケットを開きません。APKのnativeLibraryDirにある専用backendをsu経由で起動し、root supervisorがVPNと接続を管理します。Android 17のACCESS_LOCAL_NETWORKを不要なUI権限として追加せず、Tailscaleのみのbindと既存ホストACLを維持します。nativeのSystem.loadは使いません。専用backendの停止・再開とファイル転送で、新targetからの起動も確認します。

検証済み：native互換性・認証・状態記録のASan/UBSan試験、release Lint（error/fatalなし）、releaseビルド。
