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

## Android x86_64追加検証

| 環境 | revision | 確認内容 | 補足 |
| --- | --- | --- | --- |
| Bliss OS（Android 13 API 33 / x86_64 / Bionic） | `8133f420cfc2705e96f10a00220a53e9af773cd4` | 検証専用Python SCM_RIGHTS brokerでPX-Q3U4の2 USB bridgeから得た2 fdを1つの`px4d`へ渡した。内蔵カードのATRとAPDU 10/10を確認。地上波64,885,004 bytes / 345,133 packets、衛星85,902,652 bytes / 456,929 packetsを各30秒受信し、alignment/sync/malformed/TEI/continuity error 0。全8 receiverの15秒同時受信は全exit 0、sync/TEI/continuity/queue-drop/USB error 0。受信中切断は`px4-ts`と`px4d`が`DISCONNECTED`・exit 7で有限終了し、process/socket残留なし。OS再起動なしの再接続後もカード・地上波・衛星が成功。 | IP3 GT1、kernel 6.1.112-gloria-xanmod1、Termux 0.118.3。NDK r27 / API 24 build。正式なTermux用2 fd launcherは未実装。カードの物理抜去・再挿入は未試験。Release配布対象外。 |

## CIのみ

- Linux x86_64/aarch64 × glibc/muslはbuild、artifact audit、最終archive起動をCIで確認。aarch64/muslのUSB実機およびIFD loadは未確認。
- Android x86_64はbuild/ELF検査に加え、検証専用brokerを用いたBliss OS実機試験を完了。正式なTermux用2 fd launcherは未実装で、Release配布対象ではない。

## 既知の観測事項

- 2時間の8 receiver soakではreceiver 0〜6はtransport error 0。receiver 7でTEI 10,974、continuity error 453、sync/queue-drop/USB error 0。同じ約11k TEIの署名は同一個体の参照カーネルドライバでも再現。原因および他個体での挙動は未確認。
- FreeBSDでは接続直後に片bridgeのfirmware version queryが1回TIMEOUTする事象を2回観測。再試行後は正常。
- Windowsは本プロダクトのサポート外。
