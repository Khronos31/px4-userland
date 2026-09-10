# OS・環境別の検証結果

本ドキュメントは特定revisionにおける実測記録であり、将来版やすべての実行環境における動作を保証するものではありません。

## 2026-09-05 共通回帰

| 環境 | arch/libc | revision | 確認内容 |
| --- | --- | --- | --- |
| Home Assistant OS | x86_64 / Alpine musl userspace | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。地上波と衛星を各3 cycle、30秒sampleで確認。両bridge、8 receiver、終了時socket cleanupを確認。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。後続試験で内蔵カードリーダーも確認。 |
| Latitude 5300 / AnduinOS | x86_64 / glibc 2.43 | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。native Release build、CTest 7/7、地上波と衛星を各3 cycle。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。試験時はUSB node権限のためhardware操作のみsudoを使用。 |
| M2 Mac mini / macOS 26.6.2 | arm64 | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。地上波と衛星を各3 cycle。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。後続試験でPC/SC IFD、内蔵カード、物理切断と再接続を確認。 |
| Pixel 9a / Android API 37 / Termux | aarch64 / Bionic | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。UsbManagerから得た2 fdで地上波と衛星を各3 cycle。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。 |
| Google TV Streamer / Android API 34 / Termux | armeabi-v7a / Bionic | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。2 fdで地上波と衛星を各3 cycle。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。 |
| Google TV Streamer / ad-hoc APK | armeabi-v7a / Bionic | `b764d2ae15e4519db9ca9577cfae3dd8a8f1a2ff` | PX-Q3U4を使用。Android USB Host API所有の2 fdで地上波と衛星を各3 cycle。各sampleは188-byte alignment、sync/malformed/TEI/continuity/queue/USB error 0。後続試験で内蔵カードのdirect IPCを確認。APKはこのリポジトリの配布物ではない。 |

## Linuxディストリビューション追加検証

| 環境 | revision | 確認内容 | 補足 |
| --- | --- | --- | --- |
| HAOS上のDebian 13 Studio Code Server container（x86_64 / glibc 2.41） | — | PX-Q3U4を使用。static CLIのsmoke、soak、物理切断と再接続を確認。 | container root実行であり、一般ユーザー権限試験の代用ではない。 |
| HAOS Supervisor管理Alpine add-on（x86_64 / musl） | — | PX-Q3U4を使用。SupervisorのUSB公開、container起動停止、`px4d`とPC/SC consumerのcleanup順、物理切断時の有限終了、再接続後の手動復帰、自動restart loopなしを確認。 | — |
| Alpine Linux 3.24.1（x86_64 / musl 1.2.6） | `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e` | PX-Q3U4を使用。一般ユーザー、両USB bridge、8 receiver同時、direct APDU、musl IFD、PC/SC、終了後cleanupを確認。 | [Alpine Linuxの構成例](alpine-mdev.md) |
| Fedora Linux 42（aarch64 / glibc 2.41 / kernel 4.9.140-l4t+） | `df6a1e634e5bec11961a1f0f15eedd1da22f7fee` | PX-Q3U4を使用。配布archiveを一般ユーザーで実行。8 receiver同時30秒でreceiver 0〜6はclean、receiver 7は既知burstのみ、USB/protocol error 0、direct APDU成功、glibc aarch64 IFDとPC/SCでATR/APDU成功、process/endpoint残留なしを確認。 | SELinuxはDisabled。 |
| NixOS 26.05.9227.c25784012c99（x86_64 / glibc 2.42） | `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e` | PX-Q3U4を使用。一般ユーザー、8 receiver同時、direct APDU、glibc IFD、PC/SCを確認。 | [NixOSの構成例](nixos.md) |
| Chimera Linux rootfs snapshot 20251220（x86_64 / musl / libc++） | `3e6e32a107566689a9b4bf223dca1e4e89f67cfa` | PX-Q3U4を使用。Clang 22 native build、CTest 7/7、8 receiver同時、direct APDU 100/100、native IFD、一般ユーザーPC/SCを確認。 | [Chimera Linuxの構成例](chimera-linux.md) |
| Fedora 44（x86_64 / glibc / SELinux Enforcing） | `29635988c5692eb9167dc082ef0b4c4e7dfb5e04` | PX-Q3U4を使用。native Release build、CTest 7/7、systemd自動起動、専用SELinux domain、一般ユーザーIPC、PC/SC、地上波・衛星、direct APDU 10/10、OS再起動後回帰、AVC拒否0を確認。 | [Fedoraの構成手順](../../packaging/fedora/README.md) |
| FreeBSD 15.1-RELEASE（amd64） | — | PX-Q3U4を使用。native build、CTest 5/5、地上波と衛星の同時受信、内蔵カードAPDU 10/10を確認。 | 現行Release対象外。 |
| OpenWrt 25.12.5（x86_64 / musl 1.2.5 / procd） | `29635988c5692eb9167dc082ef0b4c4e7dfb5e04`と同内容 | PX-Q3U4を使用。static/stripped成果物、地上波と衛星の同時受信、APDU 10/10、procd起動停止、物理切断時exit 7・process/socket残留なし、再接続後復帰を確認。 | OpenWrt向けソース修正なし。 |
| Gentoo Linux 2.18（x86_64 / glibc 2.43、kernel 6.18.48-gentoo-dist-bin、GCC 15.3.0、OpenRC） | `fa45792787905d3a86a8cab0bbc9ab7c860c665e`後の未commit差分 | PX-Q3U4を使用。native build（PCSCなし 92/92 targets・CTest 5/5、PCSCあり 98/98 targets・CTest 7/7 PASS）。一般ユーザーでの直接カードAPDU 10/10、地上波・衛星単系統smoke、標準OpenRC `pcscd`経由のreader列挙・ATR・APDU SW9000を確認。8 receiver同時30秒+APDU 10/10はreceiver 0〜6が全エラー0、receiver 7のみ既知個体burst（TEI 10,935、continuity 652、queue/USB error 0、rc8 `PROTOCOL_ERROR`）。receiver 6・7の各30秒単独比較でもreceiver 6は全エラー0、receiver 7のみTEI 10,920・continuity 594を再現。daemon rc0、終了時のprocess/socket残留なしを確認。 | Gentoo/OpenRC向けソース修正なし。一般ユーザーはusbグループ所属でUSBノード（root:usb 0664）をsudoなしで利用可能。pcsc-lite 2.4.1（pcscd:pcscd実行、reader設定dirをpcscd所有に設定）、pcsc-tools 1.7.4を使用。 |

## 2026-09-10 Android正式launcher実機検証（レビュー前候補archive）

対象branch headは`fa45792787905d3a86a8cab0bbc9ab7c860c665e`。以下は、40秒猶予およびbytecode監査の修正前に作成した、
正式launcher実機検証済みのレビュー前候補archiveによる実機結果であり、現在の最終候補archiveではない。

| 環境 | レビュー前候補archive SHA-256 | 確認内容 |
| --- | --- | --- |
| Pixel 9a（Android 17 / aarch64 / Bionic、Termux 0.118.3） | `737f2b42d9b02be9763ad186dee8129a17121fbc00c753d4e8e3bdf9516b6723` | 正式`px4-termux`で内蔵カード、地上波、衛星、TERM/INT/HUP、第2USB取得失敗、30分8 receiver、APDU 30/30、物理切断exit 7を確認。receiver 0〜6は全エラー0、receiver 7は既知burstのみ。全processとIPC endpointの残留なし、再接続後も正常。 |
| Google TV Streamer（Android 14 / API 34 / armv7a / Bionic、Termux 0.119.0-beta.3） | `d7da07acea5f676f698a86e92c0b80e87c1dbe2aca8b01414cc5db8cd24f302b` | 正式`px4-termux`で内蔵カード、地上波、衛星、TERM/INT/HUP、第2USB取得失敗、30分8 receiver、APDU 30/30、物理切断exit 7を確認。receiver 0〜6は全エラー0、receiver 7は既知burst（TEI 10,949、continuity 627、queue/USB error 0）のみ。全processとIPC endpointの残留なし、再接続後も正常。 |
| IP3 GT1 / Bliss OS（Android 13 / x86_64 / Bionic、Termux 0.118.3） | `7561b51c0b6e934b044982eac0539b5a3de556c331e59b9c6b6f062d3b8f9222` | 正式`px4-termux`で30分8 receiverを実施し、8/8 exit 0、sync/TEI/continuity/queue/USB errorを全て0で確認。内蔵カードAPDU 30/30、TERM/INT/HUP、第2USB取得失敗、物理切断exit 7、全processとIPC endpointの残留なし、再接続後のカードAPDU 10/10・地上波・衛星を確認。 |

現在の最終候補archiveはcommit/push後のCIで生成し、Stable前に正式launcherの実機回帰を行う。

Bliss OSではバックグラウンド時にTermux UID全体が凍結し、`termux-wake-lock`も同環境で`Bad system call`となった。検証中だけADBで給電中の画面常時点灯とTermux前面表示を使用し、`stay_on_while_plugged_in`は元の`0`へ復元した。最初の凍結したsoakは無効試験として上記合格値に含めていない。

## CIのみ

- Linux x86_64/aarch64 × glibc/muslはbuild、artifact audit、最終archive起動をCIで確認。aarch64/muslのUSB実機およびIFD loadは未確認。

## 既知の観測事項

- 2時間の8 receiver soakではreceiver 0〜6はtransport error 0。receiver 7でTEI 10,974、continuity error 453、sync/queue-drop/USB error 0。同じ約11k TEIの署名は同一個体の参照カーネルドライバでも再現。原因および他個体での挙動は未確認。
- FreeBSDでは接続直後に片bridgeのfirmware version queryが1回TIMEOUTする事象を2回観測。再試行後は正常。
- Windowsは本プロダクトのサポート外。
