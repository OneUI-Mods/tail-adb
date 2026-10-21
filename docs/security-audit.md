# セキュリティ監査

## 対象と脅威モデル

2026-10-17にTail ADB 0.2.1を、Galaxy S26 Ultra SM-S948Q、Android 16、One UI 8.0、ビルド`BP4A.251205.006.S948QOPS1AZHL`で監査・実機検証しました。

攻撃者として、同じtailnetから端末の待受へ到達できる未許可ホスト、ADB秘密鍵を持つがアプリでroot許可されていないホスト、接続中shellからroot管理実装へ直接到達しようとする利用者、壊れた設定値や予期しない制御メッセージを想定しています。端末のroot管理実装そのもの、Tailscaleアプリ、tailnet管理者が侵害された場合は保護範囲外です。

## 旧版で確認した重大な問題

0.1.0の追加adbdはUID/GID 2000、effective capabilityなしでしたが、SELinux contextが`u:r:ksu:s0`のままでした。このroot管理用ドメインは実機でpermissiveになっており、認証済みADB shellからアプリ側の追加許可なしに`su -c id`でUID 0へ昇格できました。0.1.0はroot境界がfail-openだったため使用しないでください。

## 0.2.1の認可モデル

認可を2段階に分離しました。

1. shell許可：acceptした接続元がアプリで許可したTailscale CGNAT IPv4と一致する場合だけ、その接続のADB公開鍵をセッション内で承認します。公開鍵をAndroidのシステム鍵ストアへ保存しません。
2. root許可：shell許可の部分集合です。接続元IP、256-bitセッショントークン、UID/GID 2000、SELinux`u:r:shell:s0`、標準入出力FDの整合性がすべて一致した場合だけ、起動時からUID 0を維持するroot brokerがコマンドを実行します。

root許可だけが残る壊れた設定はネイティブsupervisorが起動前に拒否します。UIでもrootを有効にするとshellを有効化し、shellを無効にするとrootを失効します。

## 実装した防御

- listenerを検証済みTailscale VPNの所有UID、network handle、UP状態のインターフェース、CGNAT /32へ照合。
- `SO_BINDTODEVICE`、`android_setsocknetwork()`、IP指定bindを同時に適用。
- 追加adbdを別network namespaceへ入れ、作成済みlistener以外のネットワーク待受を作れない構成。
- 追加adbdを`u:r:ksu:s0`から`u:r:adbd:s0`へ移し、UID/GID 2000、capability 0を親から再確認。通常コマンドはAndroid標準遷移により`u:r:shell:s0`で実行。
- supervisorをKernelSUの`su --ksu-no-new-privs`で起動し、その子孫がKernelSU経由で再昇格できない状態を継承。追加adbdとshellはUID/GID 2000を保つ一方、`/system/bin/su`を直接実行してもUID 2000のままです。root許可時の昇格だけを、別経路の制限付き`su`クライアントとroot brokerで行います。
- 単一トランスポート制限、ランダムセッショントークン、SCM_CREDENTIALS、SCM_RIGHTS、メッセージ長、余分なancillary data、FD有効性を検証。
- 制御FDとlistenerの`FD_CLOEXEC`を設定後に読み戻し。Samsung adbdのtransport登録に合わせ、accept済みsocketを衝突しない高位FDへ複製。
- bionicのfdsan close経路も捕捉し、切断時に接続元とセッショントークンを確実に消去。
- adbdと`libadbd_auth.so`をSHA-256で固定し、未知のOS実装では起動拒否。
- root supervisorのPID、UID、lock FDを停止操作で照合し、別プロセスを誤停止しない設計。
- 子adbdの異常終了が3回続いた場合に停止。root取得・自動起動の無限再試行なし。
- アプリcomponentはランチャーActivity以外を非exported化。バックアップ無効。常駐Foreground Serviceと通知を不使用。

## 実機で確認した結果

- 未許可ホスト：ADBは`unauthorized`、shell実行不可。
- shell許可後：`uid=2000(shell)`、`u:r:shell:s0`。
- root未許可：PATH上の同梱`su`は拒否。`/system/bin/su -c id`を直接実行しても`uid=2000(shell)`のままで再昇格しないことを確認。
- root許可後：`su -c id`が`uid=0(root)`、`u:r:ksu:s0`。
- root失効後：shellは維持、`su`だけ拒否。
- shell失効後：rootも消え、再接続は`unauthorized`。
- 端末のLAN IPv4の同ポート：接続拒否。listenerはTailscaleインターフェースだけで待受。
- 許可済みホスト一覧と現在接続元の両方から、shell/rootトグルが同じ権限状態を更新。
- リリースビルド、release Lint、署名、`debuggable=false`を確認。

## 残るリスクと制約

- 許可単位はTailscale IPv4です。tailnetでIPを再割当てした場合は許可一覧を見直してください。
- ADB TCP自体の保護はTailscaleの暗号化とACLに依存します。Tailscale外へlistenerを公開しない設計ですが、tailnet側の設定も適切に管理する必要があります。
- root隔離はKernelSUの`--ksu-no-new-privs`の意味と継承動作に依存します。未対応の`su`では起動コマンドが失敗し、待受を開始しません。root管理実装がこの制約を破る変更を受けた場合は再監査が必要です。
- 対応OSはバイナリ完全一致で限定しています。OS更新後はハッシュ追加だけで済ませず、認証callback、SELinux遷移、transport close、shell生成経路を再検証する必要があります。
- 通常起動・ソフトリブートを伴う自動起動の実機試験は、無断再起動を避けるため今回実施していません。
- すべてのADBサブコマンド、複数端末からの競合、長時間Dozeは未網羅です。

ログは公開鍵本文、秘密鍵、セッショントークンを出力しません。認証結果、接続元Tailscale IP、検証失敗の要約は`TailAdbPolicy`と`TailAdbSupervisor`タグで確認できます。

## 0.2.4の追加確認

状態通知TCPはADBと同じVPN拘束・shell許可元限定の読み取り専用窓口です。入力でコマンドやroot操作を実行する機能はありません。通知だけではhostのADB serverを終了せず、任意のPC companionが実際のADB応答を確認します。companionの停止対象は自身で起動したforeground子だけで、既存の共有serverは対象外です。同じ送信元IPの複数serverは判別できないため、利用前に同じ端末への共有接続を解放します。

停止時はroot UID・lock FD・pidfdを組み合わせ、確認後のPID再利用による誤停止を防ぎます。VPN検査の子プロセスには実行時間と出力の上限を設け、不完全な結果は拒否します。回線復旧でbackendを破棄するときは、transportと認可tokenも同時に消去します。認証・権限境界・Android 16/17のバイナリ許可条件は維持しています。追加試験の範囲は[検証記録](testing.md)に分けています。
