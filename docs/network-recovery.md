# 通信復旧とPC連携

## 端末側

`network_recovery.h` はイベント集約、絶対期限、再試行、短時間連続クラッシュを管理する純粋な状態処理です。`bounded_command.h` は固定argvの子プロセスを時間・出力上限付きで実行します。supervisorの制御FDやnetlinkイベントが連続しても、VPN所有者の再確認は期限で実行します。確認中の不完全な出力を有効なVPNとして扱いません。

同じVPN識別情報が有効な場合は、下位回線の変更だけでbackendを破棄しません。識別情報を確認できない場合は待受・接続元・認可トークンを破棄し、安全なVPNを再確認してから作り直します。一時的なbindエラーはバックオフし、ポート競合・権限エラーでは起動を拒否します。

`--network-check <Android user>` は接続先を変更しない診断コマンドです。ConnectivityServiceの情報に加え、選択ユーザーのTailscaleパッケージが実際に持つUIDを照合します。結果は `READY`、`OTHER_USER`、`MULTIPLE`、`UNAVAILABLE`、`INSPECTION_ERROR` に分かれます。アプリは確認時刻とユーザーを保存し、45秒を超えた結果や待受の未接続状態を接続済みと表示しません。

## 状態確認TCP

ADBポート+1（ADBが65535なら65534）を利用します。検証済みTailscale IP、インターフェース、network handleへADBと同じ制約でbindします。追加ポートを確保できない場合もADB自体は継続し、アプリのPC復旧コマンドは無効になります。

shell許可リストにあるTailscale送信元だけが、接続直後に次の1行を受信できます。入力コマンドは受け付けません。

```text
TAILADB_HEALTH<TAB>1<TAB>STATE<TAB>ADBD_PID<LF>
```

| STATE | 意味 |
| --- | --- |
| READY | backendが存在し、現在のtransportは未接続 |
| CHECK_ADB_SERVER | 同じ送信元がtransportを使用中。PCで実際のADB応答を確認する |
| OTHER_HOST | 別の送信元が単一transportを使用中。PCは接続を奪わない |
| WAITING | backendの準備待ち |

読み取り専用であり、通知だけでPC側の再起動を決めません。送信は非ブロッキングで、1回のループで受け付ける接続数にも上限があります。秘密鍵、認可トークン、root権限、接続中ホストのIPは返しません。

## PC側

`tools/tailadb_watch.py` はPython 3.10以降の標準ライブラリのみを使います。専用のforeground ADB子プロセスをloopbackの空きポートに作り、`--one-device` と `ADB_MDNS_AUTO_CONNECT=0` を指定します。既存の5037番サーバーを取り込まず、`adb kill-server` も実行しません。終了対象は自身のPopenだけです。

応答確認と再接続は[ADB smart-socketプロトコル](https://android.googlesource.com/platform/packages/modules/adb/+/HEAD/docs/dev/services.md)を使います。クライアントコマンドがserverを自動起動する経路を使わず、所有プロセスが不在なら接続を拒否します。最初の接続で古いtransportが存在しなくてもconnectを実行します。

端末の状態通知に到達でき、ADBの固定echo確認が失敗した場合だけ、対象端末への再接続を試します。認証中は20秒待ち、pollごとに認証を壊さないようにします。失敗が続けば専用serverを再作成し、30秒の再起動抑制を置きます。端末到達不能、WAITING、OTHER_HOSTではserverを再起動しません。到達不能時の確認間隔は最大30秒です。

スクリプト実行前に、他のADB serverから同じ端末への接続を切る必要があります。同じPCの別serverは送信元IPが同じため、状態通知だけで所有者を識別できません。共有serverへの接続は利用者自身が管理します。再作成時は専用ポートが変わり、標準出力に新しいコマンド接頭辞が表示されます。ログイン時の自動登録やシステムサービスの変更は行いません。

参考：[ADBオプション](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/docs/user/adb.1.md)、[mDNS自動接続の設定](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/adb_mdns.cpp)。
