# SM-S948Q Android 16 / One UI 8 実機基準

取得日：2026-10-17（Asia/Tokyo）

| 項目 | 値 |
| --- | --- |
| 製品 | Samsung Galaxy S26 Ultra / SM-S948Q / `m3qjpnw` |
| Fingerprint | `samsung/m3qjpnw/m3q:16/BP4A.251205.006/S948QOPS1AZHL_SJP1AZHL:user/release-keys` |
| Build display | `BP4A.251205.006.S948QOPS1AZHL` |
| Android / API | 16 / 36 |
| One UI property | `80500`（One UI 8.0） |
| Security patch | 2026-08-05 |
| Tailscale | `1.104.1-t7f4efe814-g6d75bd403` / code `298567190` |
| KernelSU | `samsung-v1.0:KernelSU` / code `32664` |
| Tailscale interface | `tun1`（検証時。実装は名前を固定せず所有UIDとnetwork handleも照合） |

## 対応バイナリ

- `/apex/com.android.adbd/bin/adbd`
  - SHA-256: `459d1f67760789bdf8eb5a7a5c1bbe409d5dd3837d408ce4d7d14e496fcb8cbb`
- `/system/lib64/libadbd_auth.so`
  - SHA-256: `d078a2db5dcfafa23c2fa6629b28fe9c673ec57b103fb396e7b8f16763329a2f`

端末から抽出したSamsungのバイナリ自体は著作物のためリポジトリに含めません。実装は起動時に端末上のファイルをハッシュ照合し、完全一致した場合だけ端末内で利用します。

## Samsung実装で確認した差異

- copied adbdは`acceptfd:`で渡した低位FDをtransportとして維持できず、接続直後にcloseした。accept済みsocketを`F_DUPFD_CLOEXEC`で128以上へ複製するとAUTH応答まで進んだ。
- transport closeはplain `close()`だけでなくbionicの`android_fdsan_close_with_tag()`を通るため、両方でセッション破棄を捕捉する必要がある。
- 追加adbdをroot管理ドメインのまま動かすとshellから既存`su`へ到達できた。`u:r:adbd:s0`へ遷移し、生成shellが`u:r:shell:s0`になることを実測した。
- UID 2000ではKernelSUの合成`/system/bin/su`を直接実行できる。`PR_SET_NO_NEW_PRIVS`だけでは昇格を防げなかったが、KernelSU固有の`su --ksu-no-new-privs`からsupervisorを起動すると、その子孫では絶対パスの`su`を実行してもUID 2000のままになった。
- `u:r:adbd:s0`からclassic `/data/misc/adb/adb_keys`を直接読む経路は実機で拒否された。Tail ADBはシステム鍵ストアを変更せず、接続元Tailscale IPに対するアプリ内shell許可を認証callbackへ渡す。

## 更新時の再監査項目

OS更新後はハッシュだけを追加しない。少なくとも次を再確認する。

1. `acceptfd:`のlistener引継ぎとtransport FDの所有権
2. `libadbd_auth` callback ABIと未知キーの認可経路
3. `adbd`から`shell`へのSELinux遷移
4. UID/GID 2000、capability 0、親PID、継承FD
5. fdsanを含むclose経路とセッショントークン消去
6. Tailscale network handle、所有UID、interface、CGNAT /32の照合
7. 未許可、shellのみ、root許可、各失効、LAN拒否
8. KernelSUのno-new-privileges継承と、絶対パス`su`から再昇格できないこと

個人のTailscale IP、端末シリアル、ADB鍵本文、秘密鍵、認証トークンはこの記録に含めない。
