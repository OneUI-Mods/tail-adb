# 待機処理と確認間隔の省電力化（0.2.8）

## 確認間隔

VPNが同じ識別情報のまま安定していれば、再確認を15→30→60秒へ延ばす。回線・VPNの関連イベントや新しいADB接続があれば最初の間隔へ戻し、250msの集約と1秒の最小間隔で確認を前倒しする。無関係な通知や接続用TCPの通信で絶対期限が後ろへ延びることはない。

未接続時の再試行は1・2・4・8・15・30・60秒。生きているlistenerがない場合は、すべてのネットワーク通知を復帰の契機にする。bindの一時失敗と連続クラッシュの既存の上限は変えない。通知を取りこぼした場合も最大60秒の定期確認が残る。通知だけでVPNのUIDや接続先を信頼することはない。

アプリ画面が表示中の状態取得は3→6→12→15秒へ延ばす。実際の状態通知では1秒後に確認を前倒しし、画面が非表示になれば終了する。進行中の取得と通知が競合しても、別の取得ループを増やさない。停止時の割り込みをroot権限エラーとして保存しない。

## 1回の確認を軽くする

`dumpsys connectivity networks`で現在のネットワーク部分を取得し、要求履歴等を含む全体ダンプを避ける。確認するNetworkAgentInfoの項目は従来と同じ。TailscaleのパッケージUIDは`--user all`で1回列挙し、各VPNの実際の所有UIDと照合する。カンマ区切りとユーザーごとの行形式を検証し、異常出力から一部だけを採用しない。列挙が不完全な端末では従来のユーザー別照合へ戻る。次の確認へ結果を持ち越すキャッシュではない。

listenerがある間は、選択VPNのlink/IPv4/route変更、新しいVPN候補、通知欠落・不正形式を再確認の契機にする。無関係な物理回線のroute更新だけでは重い確認を起こさない。鍵変更の監視は独立しており、従来どおり既存backendを破棄して再生成する。

画面向け状態と接続元が変わらなければ、同じSharedPreferences保存と同じbroadcastを繰り返さない。画面は確認時に更新し、時間で失効する診断表示も再評価する。

## 変更しない範囲

接続元とrootの許可、公開鍵・token・選択ユーザー・VPN所有者・アドレス・network handleの検査は維持する。TCP keepaliveと転送時の45秒USER_TIMEOUTも変更しない。省電力モード、Tailscale、通知、他アプリ、表示リフレッシュレート、OSの設定には触れない。新しい常駐Android Serviceやwakelockは追加しない。

## 検証

nativeの試験は安定時の上限、変化時の復帰、イベントの連続、通信断の再試行、エラーによる停止を確認する。実supervisor関数に合成netlinkメッセージを渡し、無関係なroute、新しいVPN、選択VPNの消失、通知欠落を区別する。UIDの多ユーザー出力・不正出力も回帰試験に含める。Kotlinの画面取得は、遅い取得中の連続通知でも単一ループで終わり、close後に再開しないことをJVMで確認する。

実機では同じ条件の待機時間に対するsupervisor/子プロセスのCPU時間とcontext switchを比較する。これは電池のmAhや消費電力の直接測定ではない。充電中の電池電流を端末消費とみなさない。

Android 16/17の既存分岐を維持する。今回のSamsung Android16実機、GUI、回線の実切り替え、長時間放電による電池持ちの改善量は未検証。

接続情報だけをdumpする引数は[AOSP ConnectivityService](https://android.googlesource.com/platform/packages/modules/Connectivity/+/refs/heads/main/service/src/com/android/server/ConnectivityService.java)の`NETWORK_ARG`と`dumpNetworks`を参照。
