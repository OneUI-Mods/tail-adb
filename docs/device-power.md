# 端末側の競合接続待機

0.2.6では、単一transportを使用中に届く追加接続を、端末側で認証前のまま待機させる。PCのOSや専用スクリプトの有無に依存せず、使用中の接続を維持しながら、即時切断による短周期の再接続を抑える。

## 待機と認証

AOSP adbdの専用`server_socket`スレッドで、追加ソケットと私有の通知pipeを`poll`する。周期タイマー、追加の監視スレッド、受信したCNXNの読み取りはない。先行するtransportが閉じると、そのclose処理がpipeへ通知し、待機しているaccept処理が接続枠を再確認する。

待機中は`active_fd`、接続元、認可トークン、supervisorへの接続通知を変更しない。枠が空いた後にだけ新しいトークンを生成し、既存のshell/root許可判定へ渡す。古い接続のトークンはclose時に消去する。待機PCが先に切断した場合は、Linuxの`POLLRDHUP`で検出して待機を終了する。

pipeはnonblockingかつclose-on-execで、shell子プロセスへ引き継がない。通知が蓄積した場合は集約して読み取り、通知側を待たせない。受け付け済み待機ソケットは専用listenerスレッド内の1本で、それ以外は既存の有限listen backlogに留まる。

## 適用範囲

単一transportの制限と、許可済みホストごとのshell/root境界は維持する。既存利用者を切断して後から来たPCへ譲る動作にはしない。利用中のPCが接続を保持している間、別のPCの接続コマンドは待機やタイムアウトを表示し得る。

この処理は、空いていない接続枠への即時切断・再接続の繰り返しを抑える。接続元PCが設定する通常のTCP keepalive間隔を端末から変更するものではない。PCが接続を自ら期限切れにする場合には、その周期で再試行が起こり得る。

## 検証

`tools/test-native.py`は通知の競合、未認証データを読み取らないこと、通知の集約をsanitizer付きで検証する。Linuxでは待機PCの切断も検証する。`tests/session_wait_integration.c`は実際のpolicy関数とTCPを使い、利用中の接続の継続、引き継ぎ、新しいトークン、待機PCのキャンセルとトークン消去を確認する。

AOSPの専用listener構造は[Android 17のソース](https://android.googlesource.com/platform/packages/modules/adb/+/refs/tags/android-17.0.0_r1/daemon/transport_socket_server.cpp)を参照。実機で確認した範囲は、別途保存する検証記録に従う。
