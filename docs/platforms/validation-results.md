# OS・環境別の検証結果

本ドキュメントは特定revisionにおける実測記録であり、将来版やすべての実行環境における動作を保証するものではありません。

## 記録方法

Stable release の検証記録は本ファイルへ日付付きで追記する。新しい records directory や template framework、汎用スクリプトは作らない。release record には候補 version/commit/CI run、9 archive（8 binary + source）と checksum/audit結果、baseline tag と各 artifact の byte-identity 判定、変更の hunk-level 影響（call-path/guard）、claim ごとの `継承` / `今回再検証` / `未認定` / `対象外`、canary/soak の選定理由（環境ID E01–E17、固定順の位置、単一OS規則による非該当を含む）、各 test の環境ID・host・device/USB ID・runtime/access path・archive SHA-256・UTC時刻・コマンド・counter・結果・ログ保存先、未実施または非該当の物理操作と理由を記録する。canonical 環境ID と手順は [`release-validation.md`](../release-validation.md) を正本とする。Android ad-hoc APK は dtv-android 所管であり本記録に含めない。

## 2026-10-08 v0.1.10 release-candidate試験

候補sourceは`68b896b9d99e948aa8509d6a663a5fec3a9a5971`。独立したCI run
[`37602531584`](https://github.com/Khronos31/px4-userland/actions/runs/37602531584)（push）と
[`37602547644`](https://github.com/Khronos31/px4-userland/actions/runs/37602547644)（dispatch）は全job成功。
unit/offline、8 target build、static relink、license/source/archive audit、downloaded executable start、
4 artifact smokeを含む。9 archive本体と外側`SHA256SUMS`はbyte-identical。
外側checksumのSHA-256は`4fb81f02732fd986b4fb6915ec49844f0742283ca6546c2892a722903c10deae`。
公開v0.1.9（source/tag commit `cf38742618bb02db41a95def619fbff50e9eb0f3`）の9 archiveとは全件byte-different。

runner imageはUbuntu x86_64の`20260927.320.1` / `20261004.327.1`、Ubuntu arm64の
`20260927.135.1` / `20261004.142.1`がjob/runにより異なる。macOS arm64は両runとも
`20260831.0302.1`。artifact生成toolchainの観測値は両runで一致（37件のapk package versionを含む）:
musl GCC14.2.0 / binutils2.44-r3 / musl1.2.5-r12、glibc IFD GCC10.2.1、
AppleClang15.0.0.15000309 / Xcode15.4 (`15F31d`) / macOS SDK14.5、
Android NDK r27d (`27.3.13750724`) / Clang18.0.4 / API24。
offline Ubuntu testsはGCC13.3.0。runner metadataと実効build inputを区別してSPEC10.5-2の一致と判定した。
libusb1.0.30 sourceはworkflowのpinned checksum
`fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf`を使用。
glibc IFDのDebian11 imageはdigest
`6f519a81440354a85eb592c5f32109ab80605f6b892455983a6f618bf87fabe9`に固定。
全CLIはlibusb静的リンク。
Linux CLIはmusl完全静的（PT_INTERP/DT_NEEDEDなし）、IFDはglibc2.31またはmuslのlibc別shared object。
macOSは標準system library/frameworkのみ、AndroidはBionicのlibc/libdl/libm、daemonのliblogに依存する。
各archiveに`manifest.json`、`evidence/binary-audit.json`とlink inventory、license/noticeを同梱し、
source archiveとx86_64/aarch64/macOSのrelink proofをCIで確認した。firmwareは配布に含めない。

### artifact別短時間matrix

以下のSHAは`px4-userland-0.1.10-<target>.tar.gz`。全試験は候補archiveをそのまま使用し、手元buildで代替していない。
Q3U4は同一個体（base serial `00001205000960`、USB `0511:084a`のhalf `601/602`）、B-CAS、
両RF lead、15V adapterを使用。firmwareは2169 bytes、SHA-256
`5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`。
日時は2026-10-07 UTC（JSTでは一部10-08）。raw log保存先はHAOSの`/config/.work/px4-0.1.10/`配下。

| target | archive SHA-256 | 環境・日時 UTC | log |
|---|---|---|---|
| linux-glibc-x86_64 | `05ab1da963fffc659871a084f7d9429551c346b4fc1b6072f8e33f4a9a26f074` | E03 Latitude、AnduinOS2.0.4/glibc2.43/kernel7.0.0-34、13:20:30〜13:29:51 | `e03-matrix/` |
| linux-musl-x86_64 | `39059a44dd6fac7b7df6f1bff9f980cec2a00c576c04a18196e96591db01d7ac` | E02 HAOS Supervisor Alpine試験addon、14:27:35〜14:34:29 | `e02-matrix/` |
| linux-glibc-aarch64 | `bcd3ed3cfd60ea670d9d9f5f903e58bae97999055b56e3524bd9a3e3d683eb72` | E15 Switch、Fedora42/L4T4.9.140、14:44:34〜14:48:09 | `e15-glibc/` |
| linux-musl-aarch64 | `2674555321c65db32d032941797e1ea640802b701dbd48619effa655fef0f7ab` | E15同host、native Alpine3.22.5/Podman5.8.2、14:49:04〜14:54:05 | `e15-musl/` |
| darwin-arm64 | `793594c33c13e8bad55766e2e87a00fcf02a95f89c3843ac5aefadc59a275b2d` | E04 M2 Mac mini/Darwin25.6.0、16:38:57〜16:42:38、追加試験下記 | `e04-short-runtime/` |
| android-aarch64 | `1507ebe574c8f468f4d908bb553353ce886b4ecb6d696c03e90998b322f8e442` | E05 Pixel9a/Android17/kernel6.1.162、15:11:07〜15:15:26 | `e05-matrix/` |
| android-armv7a | `3dca801c98bb4e69e50ea6c1cda10100eae6d7a781e790b24ac10030d4bae2bd` | E06 Google TV Streamer/Android14/kernel5.15.180、15:35:52〜15:41:20 | `e06-matrix/`, `e06-recovery/` |
| android-x86_64 | `380c4eb80916936f08524775ac274d233a615678e4c1a680a2b9eec4f935635d` | E07 Bliss OS/Android13/kernel6.1.112、16:05:59〜16:12:15 | `e07-matrix/`, `e07-recovery/` |
| source | `1ea4cde0fbd9354b7de88c1efda5b5317c47c377fd3fdaf9f3327e91ca5b5f11` | corresponding source / CI audit | `run1/`, `run2/`, `ci-logs/` |

コマンドは`docs/release-validation.md` §0.1の`px4d --list/--list-json`、daemon起動、
`px4ctl status/list/card-status/card-atr/card-reset/card-apdu 90:30:00:00:00 --repeat 10`、
8 receiver各30秒の`px4-ts`、USB切断clientと再接続後の新daemon、TERM/wait/runtime残留確認。
Linux/macOSは既存`matrix-posix.sh <archive> <FW> <log>`、Termuxは正式`px4-termux`の2-FD経路を
使うリポジトリ外`termux-matrix.sh`で実行した。受信はS1318000kHz slot0 / T527143kHz、
順序0,1,4,5,2,3,6,7。通常matrixではreceiver0〜3に`--channel BS15_0/T22`を併用した
（CLIのT22変換値は527142kHz、直接指定のTは527143kHz）。
USB/cardの物理操作は通知後にユーザーが実施し、各応答は5分以内。
全8 archiveで列挙/ready8、カード不在・generation更新・NO_CARD exit9、再挿入後ATR/reset/APDU10、
USB切断のclient/daemon exit7、再列挙・新daemon受信/APDU、通常daemon exit0/runtime除去を確認した。
receiver0〜6のTS sync/TEI/CC/queue/USBはE04の下記事象を除き全0。
receiver7は下記の既知burstによりexit8となる。全receiverのbytes=packets×188。

receiver7の各matrix 30秒区間（TEI/CC、sync/queue/USBは全0）:

| target | 初回 / card再挿入後 / USB再接続後 |
|---|---|
| E03 glibc x86_64 | 11008/619、10945/606、10960/616 |
| E02 musl x86_64 | 0/0、10920/620、10946/583 |
| E15 glibc aarch64 | 10903/670、10913/622、10908/575 |
| E15 musl aarch64 | 10902/621、10944/596、10937/589 |
| E04 macOS | 10967/619、10966/659、10950/598 |
| E05 aarch64 | 10938/628、10993/695、10899/644 |
| E06 armv7a | 10929/603、10981/680、10941/586（別recovery） |
| E07 x86_64 | 10986/639、10977/616、10936/610（別recovery） |

E02は一時candidate overlayで実行し、PC/SC smokeも成功（receiver7 TEI10874/CC646）。
終了後Dockerfile/payload/Supervisor optionsを退避とbyte照合して復元し、元contextでrebuildして停止。
imageのbyte同一性を検証した意味ではない。E15 muslはnative aarch64、USB passthrough、
`--network=none --cgroups=disabled`の一時containerを用い、終了後container消滅を確認した。
E06/E07の最初のUSB再接続は新USB pathのPermission deniedで未完了。失敗を保存したうえで両pathを
`termux-usb -r`により許可し、別recovery runで8受信/APDU/終了を完了した。
E07はOS起動USBを残し、sysfs vendor/product/half serial照合によりQ3の2 pathだけを選択。
E05/E06/E07では追加のSIGINT/SIGHUP試験が各130/129で有限終了、第1 real FD取得後の第2 open失敗注入が
exit70で1秒以内に終了し、追跡した子process/endpoint/可視FD holderの残留なし。
Androidの/proc可視範囲を超えたFD不存在は主張しない。SIGTERMは各matrixで確認した。

### receiver7のfresh参照とrelease判断

stream寿命変更があるためE03でfresh比較を実施。同一個体/RF/電源/FW、S1318000slot0/T527143、
8同時受信の各30秒を比較した。候補`r7-candidate-fresh/`はTEI10915/CC563、
`px4_drv`参照`r7-reference-public30/`はTEI10935/CC656。候補のpublic測定区間は30.002〜30.027秒、
参照は30.002〜30.049秒、8 receiverの共通区間は各29.638秒/29.041秒。
receiver0〜6は両者sync/TEI/CC0。参照helperのqueue/USB counterは未観測、kernel logに追加エラーなし。
候補のqueue/USBは0。1組の観測では候補が参照以下だが、他の候補試行には参照値を上回るものもあり、
全試行の非悪化やversion間の統計的優劣を証明した意味ではない。

参照moduleはpx4_drv `7fa9f05d2cbdf1d821f479248d561f9868051b8b`と既存互換patchを使用。
最初のloadはkernel7のUBSANが末尾`chrdev[1]`宣言を検出したためcapture前にunload。
scratchでflexible array/struct_sizeの互換修正だけを適用（sanitizerを維持）、module SHA-256
`116c487c2d0bebc0b6f0c6715339e4055f1455a0b69bb41fc6980aabf6ca2337`（v0.4.0、gcc15.2.0、vermagic7.0.0-34-generic）。
helperはstartup後のpublic TSを30秒計測するよう時間処理を修正。source SHA-256
`e4b0b0f50a2b647b3e46049a75756504de31795a878f192003e0c3deac0493c9`、binary
`bed858ec52d85d6e87b656e4487f9a893a0b0f4a2478dd8a253cf24aa3f119e6`。
legacy T周波数番号は72（95143+72×6000）、Sは7。先のT22指定失敗と約28.6秒public測定試行は比較から除外し保存。
全試行後module/node消滅、候補T受信3秒/APDU10/通常終了を確認（`r7-post-unload/`）。常設installなし。

2026-10-08ユーザー決定: **receiver7は[Issue #1](https://github.com/Khronos31/px4-userland/issues/1)の
既知不具合として継続し、v0.1.10のブロッカーにはしない**。SPEC v0.29 §10.5に当該release限定の
受入判断を記録。counter/CLI exitと元の比較結果を保持し、一般のhardware認定範囲を拡大しない。

### macOSの単発CC事象と固定回数追加調査

E04初回`e04-matrix/`は長い既定TMPDIRのUNIX socket pathでINVALID_ARGUMENTとなり、
短い`TMPDIR=/tmp`で別matrixを実施した。16:42のUSB再接続後8受信ではreceiver0/1のCC各5、
receiver2/3のCC各6を観測（TEI/sync/queue/USB0、exit8）。receiver4〜6は0。
カード/終了は正常。原因は未解明であり、元試行は失敗記録を保持する。
単発の追加受信、候補USB再接続1回、公開v0.1.9 USB再接続1回はreceiver0〜6全0だった。
旧版は公開asset SHA-256 `206d4898e9e8935937a8f34a682b4bbaa1413696ece810a0b3eefeb813421871`を使用した。

ユーザーと固定回数計画を決め、候補でA=USB再接続、B=USB保持・daemon TERM/再起動をABBA×5、
各10回実施（17:08:20〜17:38:55 UTC、`e04-frequency-candidate/01-A`〜`20-A`）。
各回は単独S受信5秒→遷移→3秒待機→新daemon ready→8 receiver各30秒、途中/終了後APDU10。
同じMac/USB port/RF/電源/B-CASで、選局は全receiverを直接周波数指定に統一した。
途中の正常結果で打ち切らず20回を完走した。

| 条件 | 完了 | receiver0〜6の異常試行 | 同時stream8 / APDU10 / 通常終了・残留なし |
|---|---:|---:|---|
| A USB再接続 | 10/10 | 0/10 | 全回正常 |
| B USB保持・daemon再起動 | 10/10 | 0/10 | 全回正常 |

receiver7は別集計（A10/10、B6/10でburst）、追加試験のTEI/CCを全試行順に記録:
10994/576、10950/586、10939/632、10977/594、10877/609、10972/643、10943/647、10982/660、
10896/529、10934/618、0/0、10949/599、10898/634、10947/592、0/0、10956/603、
10926/601、0/0、0/0、10971/660。全sync/queue/USB0。
uptime/vm_statを各試行前後に保存。Aの観測USB不在は20〜69秒、readyまで1〜2秒。
0/10は当該条件で未再現という結果であり、低頻度・不存在の証明ではない。
元の失敗は一部`--channel`指定のため、そのCLI記法差の影響も排除したとは主張しない。
事前計画の条件に従い、新しいreceiver0〜6異常が出なかったため旧版の追加10回は省略した。
2026-10-08ユーザーは追加調査をここで終え、元の単発事象を保持したまま非blockingで進める判断に同意した。

### 変更impact・claimと追加確認

baselineはv0.1.9および本ファイルの2026-09-29/30 model別qualification、2026-10-02 artifact/adapter記録。
累積差分はv0.1.9..68b896b。ハンクとcall pathの分類:

- `q3u4_stream.cpp`のpump threadをdetachからjoinへ変更し、`join_in_progress`と終了待機のownershipを修正。
  共通data planeを使う全profileのstream/lifetimeに影響する。全artifact matrixとfresh r7比較を追加。
- `q3u4_tuner_backend.cpp`の切断通知をfrontend/powerへ接続し、`mark_disconnected`がreceiver/TSID状態を破棄。
  `q3u4_frontend.cpp`の各操作のdisconnected guard、`q3u4_power.cpp`の電源状態失効はQ3系のhotplug/cleanupに影響。
  W3系など未所持profileへQ3の結果を外挿しない。
- `q3u4_frontend.cpp`の衛星TSID選択は残時間0でもread-backを1回行う期限処理。
  `single_receiver_frontend.cpp`はISDB-T lock後待機と衛星slot/TSID選択を追加。
  M1URのT/S・slot/stream-id、S1URのT、Q3 same-lease retuneで確認。
- `px4_ts_core.cpp`は`--channel`の厳密parse/周波数・slot変換と排他検査。IPC wireは不変。
  offline CLI testsと実機matrixの混在指定を使用。versionは0.1.10。
- card core/IFD/IPC wire/USB discovery/packaging/LNB GPIOへの直接変更はない。
  shared backend寿命に関わる受信中APDU/終了をfreshで確認し、native adapter固有経路の非変更部分を継承。

| claim/path | 状態 | 根拠・範囲 |
|---|---|---|
| Q3U4、8 archiveの短時間stream/card/hotplug | 今回再検証 | 全8件実施。receiver7とMac単発事象の上記dispositionを伴う |
| M1UR/S1UR、E01 SCS/E03 native T/S・retune・USB復旧 | 今回再検証 | `single-profile.sh`、下記4件。既存profile認定のaffected pathを再確認 |
| M1UR/S1UR、既存native PC/SC adapter固有経路 | 継承 | IFD/IPC契約・card実装非変更。既存qualificationとnative adapter記録。今回の4件はdirect APDUでありPC/SC再試験ではない |
| Q3U4、native PC/SC adapter固有経路 | 継承 | 同じ非変更根拠。E02のみ今回PC/SC smokeを追加。E15 adapterは未試験 |
| Q3U4、same-lease S retune | 今回再検証 | E03 receiver0、BS15 slot0→1→2→1→0を各10秒。TS/STREAM_END全error0、release成功、APDU10/終了正常 |
| DTV02A-5TS-P / MLT5、既存stream hardware claim | 未認定 | 共通pump寿命変更で過去claimが失効。今回実機なし。ユーザー指定によりREADME表は保持し、release notesに当該versionの未再検証を明記 |
| その他未所持profile / 未観測runtime・feature | 未認定 | W3/MLT8/DTV系、Android M1UR/S1UR追加認定等の実機試験なし。新claimなし |
| Windows / FreeBSD / Android ad-hoc APK | 対象外 | SPEC scopeとdtv-android所管に従う |

M1UR/S1URのexact candidate追加試験日時:
E01 M1UR10:07:57〜10:14:26（`e01-m1ur/`）、E01 S1UR12:45:32〜12:49:17（`e01-s1ur-2/`）、
E03 M1UR13:07:27〜13:12:06（`e03-m1ur/`）、E03 S1UR13:12:16〜13:16:17（`e03-s1ur/`）。
T受信/APDU併走、stop/reopen、M1URのBS slots0/1/2（TSID0x40f1/0x40f2/0x48f3）、stream-id16626、
slot11 timeout5、S1URのBS拒否2、same-lease、USB抜去exit7/新daemon受信APDU/残留なしを確認。
これら4件で物理card hotplugや新しい30分profile認定を実施した意味ではない。
retune helper source SHA-256 `36d6cdfeed6163eccfc5423889e22955218ac3b97a3c090efdeb1735968152b1`、
既定binary `a9b4b74d1e0167b3699a81500b862710143a81278c1e3b764ca0b16f482404c0`、
T_ONLY `c811e517b9497b94c212eeaef4f8199882c3552d4b0ade77b29b2fb69fafd298`、
S_ONLY `960d32c0778c9d56b86ab48425b4f2223565285130ac48e009076ec8efa66863`。
既存helperを候補core/ipcへrelink、Debian clang19.1.7。Q3の結果は`q3-same-lease/`。

soakは変更影響を提示したうえでユーザーが「実施しない」と決定。固定20回は各30秒でありsoakを代替したとは扱わない。
失敗・未完了試行: E01抜去判定の誤り2件、S1UR中断1件、E03 M1UR物理応答5分超過1件、
参照module/helper/周波数/測定時間の先行試行、E15 containerネットワーク準備失敗、E06/E07 permission失敗、
E04長いruntime path失敗と単発CC事象をそれぞれ別logに保持。
受信中の列挙でinvalid_serialになる既存事象は[Issue #48](https://github.com/Khronos31/px4-userland/issues/48)、
MLT5系切断通知の未対応は[Issue #47](https://github.com/Khronos31/px4-userland/issues/47)で継続。

本節は候補実機試験と受入判断の記録。結果commitへのtag付け、最終CI、配布payload比較と公開asset確認は別工程。

## 2026-10-02 v0.1.9 release-candidate試験（必須matrix完了）

候補source commit `0353fba362c4a64331738c2fb246cd9576bdfdf8`（`docs: codify release validation procedure`）に対し、release-candidate workflow run [`36878304743`](https://github.com/Khronos31/px4-userland/actions/runs/36878304743) と独立run [`36879005809`](https://github.com/Khronos31/px4-userland/actions/runs/36879005809) を実行した。両runは全job success。各候補で8 binary archiveとsource archiveのchecksumを照合し、9 archive本体と外側`SHA256SUMS`は2 run間で全てbyte-identicalだった。`SHA256SUMS`のSHA-256は`e38cb6638e87d169e7228cde9855ac3ebfbbc68fd75aef6a5ec404b01f76c895`。v0.1.8 Stable（tag commit `817d9c6952d71b1c85c815e71c25f6170554da18`）との比較では、8 binary archiveとsource archiveの全9件がbyte-different。今回のLinux glibc x86_64 archiveは`1ada505e9b0ca7071226ce32821862cdd131d5f4f7dc5d68d4f38d24ed9af80b`。

CIではLinux x86_64/aarch64のglibc/musl、macOS arm64、Android 3 ABIのbuild/smoke、archive audit、offline test、static relink proofが成功した。候補artifactはversion `0.1.9`。ローカルの補助確認ではfresh configure/buildのCTest 8/8、`scripts/test-packaging.sh`、`scripts/test-libusb-compatibility.sh`、GNU C++を使った`test-static-relink.sh`が成功した。ローカルbuildは試験候補の代替に使っていない。

v0.1.8からの主なcode path変更は次のとおり。`identity.cpp`の`group_q3u4_devices` / `select_ready_q3u4_group`がmodel別・USB topology別候補を保持し、`select_q3u4_group_by_usb_paths`が明示path選択を解決する。`libusb_transport.cpp`のnative acquire経路と`px4d_args.cpp` / `px4d.cpp`が`--usb-path` / `--instance`および曖昧serialの拒否を接続する。`px4d_list_format.cpp`は既存text listへserial uniqueness / LNB capabilityを追加し、独立JSON formatterが同じenumerationを出す。`posix_ipc.cpp`のruntime layout / serial endpoint leaseは通常serial endpointと複数instanceの排他関係に影響する。`identity.cpp`の`DeviceProfile.supports_lnb_15v`と`it930x.cpp`のGPIO 11 configure/readback guardが非対応profileの電源経路に影響する。Termux launcherの`--instance` forwardingは2-FD handoffに影響する。Q3U4 stream/demux実装は今回の変更対象ではないが、receiver 7 burstを観測したため、SPEC 10.2.6aに従いE03で同時8受信のfresh `px4_drv`比較を実施した。

v0.1.8 Stable公開archiveとcandidate archiveのchecksum比較（2 run間は同一のcandidate checksum）:

| Platform/archive | v0.1.9 candidate SHA-256 | v0.1.8 SHA-256 | Byte-identical |
|---|---|---|---|
| linux-glibc-x86_64 | `1ada505e9b0ca7071226ce32821862cdd131d5f4f7dc5d68d4f38d24ed9af80b` | `91fad293912c6d0575fb0ca0f6b91ae533fad8d32dd971796923a3e573d574ad` | no |
| linux-musl-x86_64 | `be56c33204e61997e9c7fd22bfa87d99afea4a724f8bfe0349ee1830cbed2522` | `468c4a91b22a5e34819661a3019d9090f6bd7b666c5576650312444634367525` | no |
| linux-glibc-aarch64 | `12938d5438d0b620ef2ca9bc89494905ab0fe7dbe2daf5d9a00e9420be5cec18` | `28006cdfa012c4470d008e1ab23c8ce9472d7d309db75ec9a5491cf8b3c5435b` | no |
| linux-musl-aarch64 | `a2b8e1e72ebca58a87360cd951ad603fdbbf57f34da9c804da9dcde04360a1f3` | `cd26e48d098bcb199a4d21bee54f7b604011dc5a55572b5ec33114b93d59aa96` | no |
| darwin-arm64 | `568394269c3213cf5a1b4c68ac46f11f50d731fcbc6a96c14d8134bfa0e10d3d` | `30fec12befc77c4cd98dbcddaf2090278b117ad1e0c9f00e32815f421f80737a` | no |
| android-aarch64 | `e515bfe8df703a4e93e8414bc370195e5beb97a888a49e9ef9853345148cc7c7` | `5bf8efdee0726feac83e8f50964c2b7fdc4ab1df7b5cfe71ab0d517d7d48cebf` | no |
| android-armv7a | `7f5fc2e6eb8e71fd5cc867ce9ee707053c2ac65a1e33589ef473eccd24e31dff` | `5ad855fe06063eb1a54064fd33bea371f07af748bd52d1d0d4b304a1c2153282` | no |
| android-x86_64 | `29439449f8743b7c9e35c35c0181136fd311822742603ff760ed46b4c3fb0711` | `ae20094b04861f9952044b43c6689a7489b110264f684c3c65b7ecfee31c3ecf` | no |
| source | `e66d2260f3b40ef6c91a3a52cac8cacd067cafc567d5a7e7f13c5cb6d85fa03c` | `a5ef8c885ca7d53d34fb6922ae8941daae781e52f9f18b7fd540fd5c7f1a33d6` | no |

#### 試験結果commit後のCI artifact payload比較

試験結果commit `024469492cbdba4d31ad8c25f31286edbc2a0f24` に対する独立したcandidate run [`36993913855`](https://github.com/Khronos31/px4-userland/actions/runs/36993913855) と [`36993953559`](https://github.com/Khronos31/px4-userland/actions/runs/36993953559) は全job success。両runの9 archive本体と外側`SHA256SUMS`はbyte-identicalで、`SHA256SUMS`のSHA-256は`e855376a9d3f093f82cd2b26c6feb44b1c2b97e8a5f8256429aca2e8e53a3d9b`。

run `36993913855` の8 binary archiveを実機試験に使ったrun `36878304743` のarchiveと展開して比較した。8件すべてで差があったのは同梱`README.md`、`manifest.json`の`source_ref`とREADME file entry、内側`SHA256SUMS`だけで、その他のfile payloadはbyte-identicalだった。source archiveは結果commitのREADME/docs snapshotを含むため異なる。これは文書更新後のpayload比較であり、実機再試験の記録ではない。

変更impactはdevice identity/profile、USB discovery/path選択、POSIX IPC endpoint lease、Termux 2-FD launcher、LNB capability/GPIO guardにまたがる。今回のsoakはユーザー判断で「実施しない」。本節はcandidate試験の記録であり、tag後のrelease CIおよび公開成果物比較は別工程で行う。

### single-bridge profileの既存support claim impact判定

PX-M1UR/PX-S1URのLinux x86_64 nativeおよびHAOS SCS Debian/glibcで既存のtuner/card/native PC/SC claimは、candidate source `0353fba`のhunk分析により通常の単独接続・serial指定経路について`継承`と判定した。baseline evidenceはcommit `2f555ff`のnative AnduinOSとHAOS SCS試験（本ファイル2026-09-29/30節）。この判定は衝突する2機種の同時運用や明示path選択を含まない。

- `identity.cpp`ではsingle-bridge profileをserialとmodelで別candidateとして保持する。1台だけが見える通常経路では該当serial/modelのready groupは1件となり、従来と同じobserved USB deviceが選ばれる。`libusb_transport.cpp`はそのgroupのobservation indexからhandleを開くため、1 observationの通常serial指定ではselection resultは変わらない。candidateではM1UR/S1UR同時列挙と曖昧serial拒否を別途実機確認した。
- `posix_ipc.cpp`の追加leaseは既存serial名endpointとIPC wireを維持し、単独daemonの通常serial endpointは他instanceとの競合がない。`pcsc_ifd_adapter.cpp`の既存`device=`は維持され、追加の`instance=`と排他的に選べる。
- `it930x.cpp`のGPIO 11判定は、PX-M1UR/PX-S1URでは旧`single_receiver`判定と同じく設定・readbackを行わない。PX-M1URのLNB 15V拒否はcandidateで実機確認した。MLT5 profileはsingle bridgeであり、profile変更後も15V capability true、multi-receiver board経路のGPIO条件は従来どおりである。
- M1UR/S1URの同一serial同時接続時に`--usb-path`/`--instance`で個別起動する経路、Termux上の両機種、PC/SC `instance=`は`未認定`のまま。candidateでの実機確認は行っていない。

E01 HAOS Studio Code Server上のDebian 13.7 x86_64/glibc 2.41で、上記run `36878304743`の`linux-glibc-x86_64` archiveを追加確認した。E01はこのarchiveの必須canonical環境E03 AnduinOSの代替ではなく、以下はE01だけのsupplemental evidenceである。試験窓は2026-10-01 UTC（2026-10-02 JST）。接続機器はPX-M1UR (`0511:0854`)、PX-S1UR (`0511:0855`)、PX-Q3U4 (`0511:084a`の内部USB device 2台)。

| Feature / path | 今回のexact-candidate結果（E01） | 判定範囲 |
|---|---|---|
| `--list` / `--list-json` | M1URとS1URを同じserialの別enclosureとして表示し、双方`serial_unique=false`。両receiverの`lnb_15v_supported=false`。Q3U4はready・8 receiver、S receiverはtrue、T receiverはfalse。JSONは2配列のobjectでcapability値はboolean。`--list-json --list`はexit 2。 | 今回再検証。 |
| 曖昧serial選択 | M1UR/S1UR同時接続で`px4d --device <shared serial>`は両候補と`--usb-path`/`--instance`要求を表示してexit 2。指定runtime pathとendpointは作成されなかった。USB interface claim自体は直接観測していない。 | 今回再検証。 |
| M1UR LNB 15V拒否 | M1URのみ接続し、`--lnb-voltage 15`をdaemon opt-inなし・`--allow-lnb-power`ありの双方で実行。両方`UNSUPPORTED`、exit 3、packet 0。15Vを許可して出力させる試験は行っていない。 | 今回再検証。GPIO無書込みの個別実測はしていない。 |
| Q3U4短時間stream/card | 8 receiver T/S混在を30秒実施。USB再接続後の採用runはreceiver 0–6がexit 0、sync/TEI/continuity/queue/USB error 0。receiver 7はexit 8、TEI 11,013・continuity 618、sync/queue/USB error 0。APDU 10/10はSW `90 00`。USB切断前の先行runでもreceiver 7はTEI 10,938・continuity 501、他counter 0だったが、そのrunはwait終了値を保存できていないため採用結果に含めない。 | E01で今回再検証。fresh同時8受信reference comparisonはE03欄を参照。 |
| Q3U4 card hotplug | 抜去時`present=no`、reader generation 1→2、ATRとAPDUは`NO_CARD`・exit 9。再挿入時`present=yes`、generation 3、ATR/reset成功、APDU 10/10・SW `90 00`。 | E01で今回再検証。 |
| Q3U4 USB hotplug/recovery | 受信中に物理USBを切断。`px4-ts`は`DISCONNECTED`・exit 7で469,172 packets、daemonはUSB_IOでexit 7。再接続後に2内部USB deviceを再列挙、同じ候補でdaemonを再起動し、8 receiverとAPDUを再確認。process、client、IPC endpointの残留なし。 | E01で今回再検証。 |

準備時の初回daemon試行2件は、runtime directoryを作らなかったため`NOT_FOUND`、続く試行はmode `0755`のdirectoryを使ったため`INVALID_ARGUMENT`で終了した。手順に従って既存runtime directoryをmode `0700`に直した後は起動し、これらを候補不具合としては扱わない。全試験後、M1UR/S1UR/Q3U4の3機種が再び列挙され、Q3U4はready・8 receiverであることを確認した。

E03 AnduinOS 2.0.3 x86_64（glibc 2.43、kernel `7.0.0-34-generic`）では、同run `36878304743`の`linux-glibc-x86_64` candidate archive（SHA-256 `1ada505e9b0ca7071226ce32821862cdd131d5f4f7dc5d68d4f38d24ed9af80b`）とfirmware SHA `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`を使用した。`px4d --list` / `--list-json`はQ3U4（serial `00001205000960`、8 receiver）をreadyとして列挙した。candidate daemonでカードpresent、ATR/reset成功、APDU 10/10・SW `90 00`を確認。30秒8 receiver混在受信はreceiver 0–6がexit 0・全counter 0、receiver 7はexit 8（TEI 11,017・continuity 630、sync/queue/USB error 0）。カード抜去時`present=no`、generation 1→2、ATR/APDUは`NO_CARD` exit 9。再挿入時generation 3・present=yes、ATR/resetとAPDU 10/10が成功した。受信中USB切断ではclientが`DISCONNECTED` exit 7で終了し、daemon停止後にprocess/runtime残留なし。USB再接続後にdevice 2台を再列挙し、同candidate daemonを再起動、ready・APDU 10/10を確認した。再度の30秒8 receiver runはreceiver 0–6がexit 0・全counter 0、receiver 7がexit 8（TEI 10,909・continuity 574、sync/queue/USB error 0）。

SPEC 10.2.6a比較では、`px4_drv` repository commit `7fa9f05d2cbdf1d821f479248d561f9868051b8b`（module version `0.4.0`）をkernel 7.0向け互換修正2点（`strlcpy`→`strscpy`、`class_create`のkernel version条件）だけ適用して一時buildした。互換patch SHA-256は`320baedcfc75800afc9dd4704cf5985e9115e51537bfee6bf86c6208796968c1`、AnduinOS GCCは15.2.0、module SHA-256は`983e53d3d0a14e5463ca5af652fcaa17cb8e1b2fb54abc0b0fec5dd61221643b`。参照moduleで8 receiverを30秒同時取得し、receiver 7はTEI 11,035・continuity 657、sync/queue/USB error 0（324,222 packets）。候補版の直前の同条件runはTEI 10,909・continuity 574で、両値とも参照を超えなかった。単独receiverの最初の比較は負荷条件が違うため採用せず、同時8受信の比較を採用した。参照capture helper source SHA-256 `cff2e475a52f808161ea38b57b21cc452dae445cdfe737fea17b7617914ff7cd`、binary SHA-256 `da11190fe75d821be71c41a84aba119ebdacec131ab6f04dd1d6ddce613ccf6d`。device nodeがroot-onlyのためhelperをsudo実行した。load/unloadは試行後すぐ行い、module unloaded、device nodes removed、candidate daemon/client/helper残留なしを確認した。記録は`/config/.work/px4-userland/e03-results/`（参照出力`r7-reference-concurrent.txt`、receiver別ログ`capture-concurrent/`、候補runとhotplug記録一式）。この同一個体・同一周波数・同一firmware・同アンテナ/電源・同時8受信の30秒比較ではcandidate burstはreferenceより悪化せず、SPEC 10.2.6aの受入条件を満たした。

E04 macOS arm64（`mac` aliasのM2 Mac mini、macOS 26.6.2 build 25G83）で、2026-10-02 15:24–15:47 JSTに同run `36878304743`の`darwin-arm64` candidate archive（SHA-256 `568394269c3213cf5a1b4c68ac46f11f50d731fcbc6a96c14d8134bfa0e10d3d`）とfirmware SHA `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`を使用した。outer checksumと展開後`SHA256SUMS`は全項目OK、manifest `source_ref`はcandidate commitと一致。`otool -L`ではpx4d/px4-ts/px4ctlにmacOS system以外のdynamic dependencyなし。`px4d --list` / `--list-json`はQ3U4（serial `00001205000960`、USB `0511:084a`、内部serial `...9601` / `...9602`、port `2-3.1` / `2-3.2`）をready・8 receiver・`serial_unique=true`として列挙した。JSONのreceiver能力値はISDB-Sがtrue、ISDB-Tがfalseでtext listと一致した。

candidate daemonでカードpresent、ATR/reset成功、APDU 10/10・SW `90 00`を確認した。抜去時は`present=no`・reader generation 2となり、ATR/reset/APDUはすべて`NO_CARD`・exit 9。再挿入後は`present=yes`・generation 3、ATR/resetとAPDU 10/10が成功した。Homebrew `pcsc-lite` 2.5.2とcandidate `ifd/px4-userland-ifd.bundle`を一時`reader.conf.d`で起動し、out-of-repository PC/SC probeでもreader `PLEX PX-Q3U4 Internal Card Reader 00 00`、ATR、APDU 10/10・SW `90 00`を確認した。probe source SHA-256 `256b42bf44cd0039f181c26133f436084ed70d76ace7c1eb438b538496ac0285`、binary SHA-256 `4d2f8b01833350a66cc22c30a0fea0e79c4fc971fbf266b54bf09d684b6fa12e`。Homebrew client shimには`LIBPCSCLITE_DELEGATE=/opt/homebrew/opt/pcsc-lite/lib/libpcsclite_real.1.dylib`を設定した。試験後Homebrew pcscd、candidate daemon/client、runtime endpoint/socketは残っていない（macOS system PCSC serviceはOS管理）。

同時8 receiverを30秒実行し、再接続後の最終確認ではreceiver 0–6がexit 0・sync/TEI/continuity/queue/USB error 0、receiver 7がexit 8（341,088 packets、TEI 10,948・continuity 590、sync/queue/USB error 0）。先行するUSB再接続後runのreceiver 7はexit 8（341,740 packets、TEI 11,023・continuity 651、他error 0）、初回runはTEI 11,029・continuity 610。いずれも同一Q3U4個体・周波数527143 kHz・30秒・同時8受信のE03 fresh reference（TEI 11,035・continuity 657）を超えず、SPEC 10.2.6aの比較条件を満たした。USB切断時のclientは`DISCONNECTED`・exit 7（430,005 packets、80,840,940 bytes）。USB再接続後に2内部USB deviceを再列挙し、candidate daemonを新規起動してready・APDU 10/10を確認後、8 receiver runと終了後APDU 10/10を実施した。daemon/client、Homebrew pcscd、IPC socket/control endpointの残留はなく、runtime directoryも削除できた。ログとPC/SC probeは`/config/.work/px4-userland/e04-results/`、candidate stagingはMacの`/tmp/px4-userland-e04-v019-20261002/`。

E07 Bliss OS Android 13 / x86_64（kernel `6.1.112-gloria-xanmod1`、Termux 0.118.3）で、2026-10-02 08:54–09:03 UTCに同run `36878304743`の`android-x86_64` exact candidate（archive SHA-256 `29439449f8743b7c9e35c35c0181136fd311822742603ff760ed46b4c3fb0711`、manifest source_ref `0353fba362c4a64331738c2fb246cd9576bdfdf8`、architecture `x86_64`）とfirmware SHA `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`を使用した。展開後`SHA256SUMS`全件OK。M1UR/S1URもBliss hostに接続された状態で、Q3U4の2 USB deviceだけ（初回path `/dev/bus/usb/001/012`・`013`、USB `0511:084a`、serial `000012050009601`・`...9602`）を`px4-termux`の2-FDに渡した。通常のTermux direct USB pathでは`px4d --list`を使わず、launcher経由の`px4ctl list`で8 receiverを確認した。

初回daemonはready・`usb-present-mask=0x03`、receiver 0–7はfree、card present、reader generation 1。ATR/resetとAPDU 10/10・SW `90 00`が成功した。最初の8 receiver 30秒同時runとカード再挿入後runはいずれもreceiver 0–6がexit 0、sync/TEI/continuity/queue/USB error 0。receiver 7は既知burstでそれぞれexit 8（341,592 packets、TEI 10,996・continuity 579）とexit 8（340,387 packets、TEI 10,945・continuity 611）、sync/queue/USB error 0。受信中statusは8/8 streaming、APDU 10/10成功。カード抜去時は`present=no`、reader generation 1→2、ATR/APDU `NO_CARD`・exit 9。再挿入時generation 3・`present=yes`となりATR/resetとAPDU 10/10が復帰した。

USB切断時、1 receiver clientは`DISCONNECTED`・exit 7（275,781 packets、51,846,828 bytes）。Termux USB listから両Q3U4 deviceが消え、launcher/daemonは`USB_IO` cleanupで停止しprocess残留なし。再接続後USB pathは`/dev/bus/usb/001/018`・`019`へ変わった。同candidate launcherを新規起動してready・8 receiver list・card present・APDU 10/10を確認。再接続後の8 receiver 30秒runではreceiver 0–6がexit 0・全error 0、receiver 7がexit 8（341,229 packets、TEI 10,950・continuity 585、sync/queue/USB error 0）、APDU 10/10成功。launcherへのSIGTERM後launcher/daemon/callback/client processおよびIPC endpoint/socketの残留なし、runtime directoryは空。`termux-info`でscreen-on system settingを照会する権限はなく、以前の`settings get system stay_on_while_plugged_in`はpermission denialだったが、今回の試験中はdaemon・captureが継続した。logs: `/config/.work/px4-userland/e07-results/e07-run-20261002/`。candidate staging: `/data/data/com.termux/files/home/tmp/px4-userland-e07-v019/`。

E04 macOS arm64で、2026-10-02 17:43:53 JSTに利用者申告のM1UR/S1UR/Q3U4同時接続を確認し、同じ`darwin-arm64` exact candidate（archive SHA-256 `568394269c3213cf5a1b4c68ac46f11f50d731fcbc6a96c14d8134bfa0e10d3d`）の実行ファイルで補足試験した。IORegistryと`px4d --list` / `--list-json`の双方で3機種を列挙。M1URとS1URはserial `000000000000001`を共有し、model・USB ID (`0511:0854` / `0511:0855`)・USB位置 (`2-3.1` / `2-3.2`)ごとに別enclosureで表示され、双方`serial_unique=false`。Q3U4はserial `00001205000960`、2内部USB device、8 receiver、`serial_unique=true`。JSON assertionでモデル順とserial uniqueness、receiverごとのLNB capabilityを確認し、全`lnb_15v_supported`がbooleanであること、M1UR/S1URはfalse、Q3U4はISDB-S true / ISDB-T falseであることを確認した。曖昧な`--device 000000000000001`指定は候補2機種と`--usb-path`/`--instance`指定要求を表示してexit 2 (`INVALID_ARGUMENT`)。新規runtime directoryは作成されなかった。USB interface claim前に拒否されたこと自体はmacOS上で直接計測していない。ログはMac上の`/tmp/px4-userland-e04-v019-20261002/collision-list.txt`、`collision-list.json`、`collision-ambiguous.out`、`collision-ambiguous.err`、`collision-ambiguous.rc`に保存。3機種同時接続中の受信・カード・LNB・電源操作は行っていない。前の未実施記録は、本補足試験により解消した。

E02 HAOS Alpine/musl add-onでは、同じworkflow run `36878304743`の`linux-musl-x86_64` archive（SHA-256 `be56c33204e61997e9c7fd22bfa87d99afea4a724f8bfe0349ee1830cbed2522`）をstagingし、Supervisor管理下の専用test add-onを再buildして確認した。試験hostはHAOS Linux 6.18.52-haos x86_64、Alpine 3.22、musl 1.2.5。試験期間は2026-10-01 UTC（2026-10-02 JST）。既存optionsはSupervisorの現行値から退避し、Q3U4用optionsを適用後、試験終了時に退避値との完全一致を確認して復元し、add-on stoppedを確認した。HA Coreとmirakcは再起動していない。

初回のM1UR profile起動は、test add-on内の固定`EXPECTED_SOURCE_REF`が旧commit `2f555ff0542c7a36fb2565b64ebc0703f44a0931`のままで、新candidateの`source_ref`と一致せず停止した。test add-onの参照をcandidate commit `0353fba362c4a64331738c2fb246cd9576bdfdf8`へ合わせてrebuildし、続いて`card_probe`へ直接`card-atr` / `card-reset`の記録を加えて再度rebuildした。add-onの`tests/test.sh`は両変更後にsuccessした。これは専用試験ハーネスの古い参照・不足記録であり、配布候補の失敗ではない。

最終E02 smoke `20261001T160904Z`は30秒、summary `passed`、daemon/PCSC/card exit 0、PC/SC reader確認あり、終了後residualなし。8 receiverは0–6がexit 0でsync/TEI/continuity/queue/USB error 0、receiver 7はexit 8、TEI 11,000・continuity 625、他error 0（既知のnon-blocking receiver-7 burst）。直接card-status / ATR / reset / APDUは全てexit 0、APDU 10/10、SW `90 00`。`20261001T154620Z`ではカード抜去後`present=no initialized=no`、APDU `NO_CARD` exit 9を確認。再挿入run `20261001T154810Z`ではPC/SC scanがinsertedとATRを記録し、PC/SC APDUと直接APDU 10/10が成功した。USB hotplug run `20261001T155008Z`は物理切断を検出し、daemon/clientを停止、residualなしで再接続待ちへ移行した。再接続後のbounded smokeはsummary `passed`、receiver 0–6 clean、receiver 7はexit 8（TEI 10,954・continuity 584、他error 0）、card/APDUとPC/SC reader確認も成功した。run directoriesは`/addon_configs/local_userland_stable_alpine_test/results/20261001T154620Z/`、`20261001T154810Z/`、`20261001T155008Z/`、`20261001T160904Z/`。

E06 Android 14/API 34 armv7a（Google TV Streamer、Termux 0.119.0-beta.3、ABI `armeabi-v7a`）では、同run `36878304743`の`android-armv7a` archive（SHA-256 `7f5fc2e6eb8e71fd5cc867ce9ee707053c2ac65a1e33589ef473eccd24e31dff`）を使用した。`termux-usb`によりQ3U4の異なる2 USB pathを開き、`px4-termux`から`px4d --fd 7 --fd 8`がreadyになった。Termuxは通常のUSB列挙を提供しないため、`--list`確認は対象外とした。card-status、ATR、reset、APDU 10/10（SW `90 00`）を確認。30秒8 receiver runはstatus上すべてstreaming、receiver 0–6 exit 0でsync/TEI/continuity/queue/USB error 0、receiver 7 exit 8（TEI 11,044・continuity 576、他error 0）。カード抜去時generation 1→2、`present=no`、ATR/reset/APDUは`NO_CARD` exit 9。再挿入後generation 3、ATR/reset成功、APDU 10/10・SW `90 00`。USB切断後`px4ctl status`は`DISCONNECTED` exit 7。launcherへSIGTERM後40秒猶予内にlauncher、daemon、両callbackが終了し、runtime endpointが残らなかった。再接続後は新USB pathで許可を再取得し、同candidate daemonがready、APDU 10/10、8 receiver再確認が成功した（receiver 0–6 exit 0・全error 0、receiver 7 exit 8・TEI 10,960・continuity 629・他error 0）。Termux上の一時candidate directoryは終了後削除し、実行ログを`/config/.work/px4-userland/e06-results/`へ保存した。

E05 Pixel 9a（Android 17、aarch64 / ABI `arm64-v8a`）では、同run `36878304743`の`android-aarch64` archive（SHA-256 `e515bfe8df703a4e93e8414bc370195e5beb97a888a49e9ef9853345148cc7c7`、manifest source_ref `0353fba362c4a64331738c2fb246cd9576bdfdf8`）とfirmware SHA `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`を使用した。実施は2026-10-02 14:06–14:13 JST。`termux-usb`の2 device pathを許可し、`px4-termux`の2-FD経由でQ3U4 daemonがready、status上8 receiver freeとなり、`px4ctl list`は8 receiverを列挙した。通常の`px4d --list`はTermux direct USB accessで`USB_IO`となるため、Termuxの2-FD access pathを試験対象とし、通常列挙は対象外とした。開始時card-statusはpresent=yes、ATR/reset成功、APDU 10/10・SW `90 00`。30秒8 receiver run中statusは全receiver streaming、APDU 10/10成功。receiver 0–6はexit 0、sync/TEI/continuity/queue/USB error 0。receiver 7はexit 8、TEI 11,005・continuity 622、sync/queue/USB error 0。カード抜去時reader generation 1→2・present=no、ATR/APDUは`NO_CARD` exit 9。再挿入時generation 3・present=yesとなりATR/resetとAPDU 10/10が成功した。USB切断時`termux-usb -l`は空、`px4ctl status`は`DISCONNECTED` exit 7。launcherへSIGTERM後、daemon/callback/processおよびruntime endpointの残留なし。再接続後に2 USB pathの許可を取り直し、同candidate daemonはready、card present=yes、APDU 10/10成功。再度の30秒8 receiver runはreceiver 0–6がexit 0・全error 0、receiver 7がexit 8（TEI 10,958・continuity 651、sync/queue/USB error 0）。終了後APDU 10/10成功、全receiver free、launcher停止後process/runtime残留なし。一時candidate directoryは削除し、ログは`/config/.work/px4-userland/e05-results-rerun/`へ退避した。receiver 7 burstについてfresh `px4_drv` reference比較は未実施。

必須matrixで今回再検証したのはE02 `linux-musl-x86_64`、E03 `linux-glibc-x86_64`、E04 `darwin-arm64`、E05 `android-aarch64`、E06 `android-armv7a`、E07 `android-x86_64`、E15 `linux-glibc-aarch64`およびAlpine container上の`linux-musl-aarch64`。E01の結果をE03へ、Androidの別ABI間で結果を継承しない。

E15 Fedora Linux 42 aarch64/glibc 2.41（host kernel `4.9.140-l4t+`、SELinux Disabled）とAlpine Linux 3.22/musl aarch64 container（Podman 5.8.2、rootful、`--cgroups=disabled --network=none`、host `/dev/bus/usb` passthrough）で、2026-10-02 18:14–18:42 JSTにrun `36878304743`のexact candidatesを使用した。glibc archive SHA-256は`12938d5438d0b620ef2ca9bc89494905ab0fe7dbe2daf5d9a00e9420be5cec18`、musl archive SHA-256は`a2b8e1e72ebca58a87360cd951ad603fdbbf57f34da9c804da9dcde04360a1f3`、firmware SHA-256は`5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`。外側checksumと各archive内`SHA256SUMS`を検証し、manifestのversion/source_ref/architectureはcandidateと一致した。glibc `px4d` / `px4-ts`とmusl版の各実行ファイルは同じSHA-256だった。対象はPX-Q3U4のみの受信/card試験。musl containerのcandidate `--list` / `--list-json`ではM1URとS1URも列挙し、共有serial `000000000000001`の別enclosure、双方`serial_unique=false`、LNB capability falseを確認した。Q3U4はready・8 receiver・serial unique、ISDB-S capability true / ISDB-T false。全JSON capability値はboolean。Fedora hostではM1UR/S1URのUSB node権限がなく`open_failed`、Q3U4はreadyだった。

glibc native candidateはQ3U4をserialで選択してreadyになり、card present、ATR/reset、direct APDU 10/10・SW `90 00`を確認した。musl containerでは3機種同時列挙中にQ3U4を明示USB path `1-1.4.1` / `1-1.4.2`とinstance `e15-musl`で起動し、ready・8 receiverを確認した。最初のserial-only試行およびpath指定試行は、host UID 1000所有のruntime directoryをcontainer rootから使ったため`INVALID_ARGUMENT`で失敗した。runtime directoryをcontainer root所有に直してからpath/instance指定で成功した。serial-only起動はcontainer内で再試験していないため未確認。これは環境準備時のdirectory ownership不一致であり、候補不具合とは扱わない。

各candidateでcard抜去時`present=no`、reader generation 1→2、ATR/APDUは`NO_CARD`・exit 9、再挿入時generation 3・ATR/reset成功、APDU 10/10・SW `90 00`を確認した。USB切断中のmusl clientは`DISCONNECTED`・exit 7（2,664,202 packets、500,869,976 bytes）で終了、daemonは`USB_IO` cleanup後停止し、IPC endpointは残らなかった。再接続後はQ3U4をUSB addresses 35/36で再列挙し、musl daemonを再起動、ready・APDU 10/10を確認した。

8 receiver混在30秒captureの最終採用runでは、glibc再接続後receiver 0–6がexit 0・sync/TEI/continuity/queue/USB error 0、receiver 7はexit 8（TEI 10,963・continuity 617、sync/queue/USB error 0）。muslではAPDUをcapture開始5–6秒後に送った2回でreceiver 0/1に各1 sync errorが出たため、そのrunは不採用として保持した。APDUなしの再試験、およびAPDUを開始15秒後に送った再試験ではreceiver 0–6がexit 0・全counter 0。APDU同時runは10/10・SW `90 00`。receiver 7は同時run間で既知burst（TEI 10,922–11,017、continuity 562–703、sync/queue/USB error 0）または一度のclean runを観測した。USB再接続後の最終runもreceiver 0–6がexit 0・全counter 0、receiver 7はexit 8（TEI 11,017・continuity 576）、受信中APDU 10/10。glibc/muslともnative PC/SC IFD/adapterは試験していない。container、daemon、client終了後に残留processはなく、runtime directoryは空。全logは`/config/.work/px4-userland/e15-results/e15-run-20261002/`に保存。

## 2026-10-01 v0.1.9 macOS再現性修正commitの独立2 run確認

source commit `78e1c36513882fa39411476c278e009862f1ac52`（`fix: make macOS px4d reproducible`）に対し、`release-candidate` workflowを独立した2回のclean CI candidate runで実行し、macOS再現性修正後の9 archiveを照合した。1本目は同commitへのpush run [`36850114564`](https://github.com/Khronos31/px4-userland/actions/runs/36850114564)、2本目は同じcommitを指定した`workflow_dispatch` run [`36850599261`](https://github.com/Khronos31/px4-userland/actions/runs/36850599261)。GitHub Actions APIで両runの全jobがsuccessであることを確認した。このpairはSPEC v0.27制定前の実績であり、次のv0.1.9 candidate pairは改訂した10.5-2受入条件に従って確認する。

両runの`release-candidate` artifactは、8 binary archiveとcorresponding-source archiveの9 tar archive、およびその外側`SHA256SUMS`を含む。各runを新しい空ディレクトリへ展開し、`sha256sum -c SHA256SUMS`で9件すべてOKを確認した。対応する9 archive本体を個別に`cmp`し、すべてbyte-identicalであることを確認した。両runの外側`SHA256SUMS`も`cmp`でbyte-identicalであり、そのSHA-256は`297b109ee800438dd2de2b9343323260404a92cc369f1b022ea0e1656ff808dc`である。正規化は行っていない。9 archiveの両run共通SHA-256は次のとおり。

| Archive | 両run共通SHA-256 |
|---|---|
| linux-glibc-x86_64 | `41f1a7ad57f1edd2fec7d437f1103846809403519cff800885504e070cc5e554` |
| linux-musl-x86_64 | `af42207b50fa90382e0a89ff069cc71fc54c67ff5000664ac501c893845ea39f` |
| linux-glibc-aarch64 | `ab96b717e47e93b2aff1214a66cdce2a051f1e94f22664bce77e09ea9395f08b` |
| linux-musl-aarch64 | `66a2ba5c8b86d01cd472ae5c3e3ee0c07b36e9ecc53fe8eaf12055b33413a628` |
| darwin-arm64 | `f6c63c83eea983d9c3f84a2a2bc73739bb17abafd257680fbd2b09901005ad85` |
| android-aarch64 | `b51ebcd611a68aeea2d9dbfd79999a6d601e9fb3b80c16453fc90ba9a50fd329` |
| android-armv7a | `e8f6e61575965f36d31c66eb71b48a1021e12853c8687297ff4cdb98252e023f` |
| android-x86_64 | `297e52f596c6b74d93909e647535dc0525ebd8e3a9b51478b7ab7cbb82381ec3` |
| source | `a1d03e56214cb4493d44af18ce583af5d20d55b5866ca2c5f650c38cc1b86cc8` |

same-input evidence: 両runは同一source commit `78e1c36`、commitで固定された同一workflow、pinned action SHA、pinned container digestを用いた。artifact build jobのrunner labelsは両runとも`ubuntu-24.04`、`ubuntu-24.04-arm`、`macos-14-arm64`、GitHub Actions runner versionは`2.337.0`。runner image versionはrunner metadataであり、artifact生成toolchainそのものではない。両runで観測されたrunner image versionの集合は、ubuntu image version `20260920.314.1`と`20260927.320.1`、hosted image version `20260828.587`と`20260901.588`、macOS image version `20260831.0302.1`。Android matrixのABIとrunner image versionの組（どのABI jobがどのimage versionで走ったか）は両run間で異なったが、観測されたversion集合自体は同一だった。linux x86_64/aarch64 jobのrunner labelsはそれぞれ`ubuntu-24.04` / `ubuntu-24.04-arm`、Alpine build container digestは両runとも`sha256:5291449c3df73caf6ed85e649dec1b9e818b39a5d8c871e97afc13e9cd5e8fa8`、glibc IFD用Debian container digestは`sha256:6f519a81440354a85eb592c5f32109ab80605f6b892455983a6f618bf87fabe9`で、Debian snapshotは`20260825T000000Z`。Android jobsは`ubuntu-24.04`上でpinned setup action `afb4c9964b521afb97c864b7d40b11e6911bd410`とNDK `r27d`を使用し、host toolとしてCMake `3.28.3-1build7`、Ninja `1.11.1-2`をaptから導入した。両runの3 ABI jobでこれらのversionが一致した。各runのGitHub Actionsログでこれらを確認した。両macOS jobのCI logで確認したbuild inputも完全に一致する。runnerは`macos-14` / image `20260831.0302.1`、macOS `14.8.9` build `23J631`、Xcode `15.4` build `15F31d` / SDK `14.5`、Apple clang `15.0.0 (clang-1500.3.9.4)`、Apple ld `ld-1053.12`、CMake `4.4.3`、Ninja `1.13.2`、pcsc-lite `2.5.1`、pkgconf `3.0.6`。libusb `1.0.30` source archiveのSHA-256は両runとも`fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf`で、取得時にCI logで照合した。artifact生成に用いるtoolchain/container/dependencyの観測version・digestは両runで一致した。run `36850114564`のmacOS inventoryはjob logの`Report macOS toolchain`（2026-10-01 10:36:07–10:36:10 UTC）、run `36850599261`は同step（10:40:46–10:40:49 UTC）。

このmatching pairは、本ファイルの先行するdarwin-arm64非再現性記録（run `36811522001`と`36826506813`の`px4d`差、LC_UUID/署名領域のbuild間変動）に対し、commit `78e1c36`での修正結果を示す。先行記録は非再現性の履歴として削除せず保持する。commit `78e1c36`に対するexact-candidate hardware canaryは未実施であり、本記録は機種hardware claimやREADME/support表示を更新しない。

## 2026-10-01 v0.1.9候補の同一serial列挙確認

CI candidate commit `fab5491365eae35a5adc0ce818092384eb5b9647` / workflow run `36811522001` の Linux glibc x86_64 archive（SHA-256 `2627d8889143c22eb45fe8217b39d9123ea9a69b9c22192d8cafbd16c42cc775`）から展開した候補バイナリを、HAOS Studio Code ServerのDebian/glibc x86_64で実行した。2026-10-01 15:33 JST、M1UR (`0511:0854`)、S1UR (`0511:0855`)、Q3U4 (`0511:084a`の内部USB device 2台)を同時接続した状態で確認した。

`px4d --list`と`px4d --list-json`はいずれも、serial `000000000000001`をPX-M1URとPX-S1URの別々のenclosureとして表示し、それぞれ`serial_unique=false`、USB port `1-2.1` / `1-2.2`を報告した。Q3U4はserial `00001205000960`の1 enclosure、8 receiver、2内部deviceとしてreadyだった。`px4d --device 000000000000001 --firmware /config/.tools/tv-tuner-setup/it930x-firmware.bin --runtime-dir <新規未作成path>`は両候補を表示して終了コード2となり、runtime pathは作成されなかった。曖昧なserialによる選択は拒否された。物理USB interface claimの直接計測はしていない。

同日、Q3U4の物理USBケーブル1本を抜き挿しした後、同じ2つの内部USB deviceがport `1-2.3.1` / `1-2.3.2`に再列挙され、変わったUSB addressで候補`--list-json`がreadyを報告した。timestamp付きのlive `--list`では、この`1-2.3.1` / `1-2.3.2`は同日05:32:09Z（14:32 JST、`q3u4-glibc-results/20261001053209/`、dev1_address=41 / dev2_address=42）ですでに観測されており、上記15:33 JSTの同時接続列挙より前である。本項は15:33 JSTの列挙の後に再列挙したことを示すものではない。これは筐体1台の再接続であり、物理ケーブル2本を抜き挿しした試験ではない。

## 2026-10-01 v0.1.9 Stable候補のリリース検証

Candidateはversion `0.1.9`、source commit `fab5491365eae35a5adc0ce818092384eb5b9647`、GitHub Actions push run [`36811522001`](https://github.com/Khronos31/px4-userland/actions/runs/36811522001)（全job success）。最新Stable baselineは`v0.1.8`（tag target `817d9c6952d71b1c85c815e71c25f6170554da18`）。Candidate 9 archivesはworkflowのchecksum/auditを通過し、`sha256sum -c SHA256SUMS`でも9件すべてOK。対応するv0.1.8 release archiveとの比較では9件すべてbyte-differentだった。

| Archive | v0.1.9 candidate SHA-256 | v0.1.8 SHA-256 | 判定 |
|---|---|---|---|
| linux-glibc-x86_64 | `2627d8889143c22eb45fe8217b39d9123ea9a69b9c22192d8cafbd16c42cc775` | `91fad293912c6d0575fb0ca0f6b91ae533fad8d32dd971796923a3e573d574ad` | byte-different |
| linux-musl-x86_64 | `b769cd41fc2c8d02f21ff1681a91b52cc46f972f9d3a28de12e31a42e6fba644` | `468c4a91b22a5e34819661a3019d9090f6bd7b666c5576650312444634367525` | byte-different |
| linux-glibc-aarch64 | `0ef13d373833cc93fec925f7992dc91c0b37c97d3856a45e6c845a3bd787ab0c` | `28006cdfa012c4470d008e1ab23c8ce9472d7d309db75ec9a5491cf8b3c5435b` | byte-different |
| linux-musl-aarch64 | `f8a56d1532c255ec5cb73bc546e390432b2cbf86d842180eb910bb5d12e094bd` | `cd26e48d098bcb199a4d21bee54f7b604011dc5a55572b5ec33114b93d59aa96` | byte-different |
| darwin-arm64 | `4a58f5fb914379059a11a789759a8609e5fef1ac9a1ed7174c5f5650bcda4b00` | `30fec12befc77c4cd98dbcddaf2090278b117ad1e0c9f00e32815f421f80737a` | byte-different |
| android-aarch64 | `825dcbaaa41006f3e8fe2bc7bd5c4651a7e2fd38aee69b9abb3af656b7b4cb23` | `5bf8efdee0726feac83e8f50964c2b7fdc4ab1df7b5cfe71ab0d517d7d48cebf` | byte-different |
| android-armv7a | `e487f5b4381274e42fa4476562e8068df9656dccf7f52bbba8b45eabad48f11c` | `5ad855fe06063eb1a54064fd33bea371f07af748bd52d1d0d4b304a1c2153282` | byte-different |
| android-x86_64 | `f0d1bc0c519793aaff4783a19a91ed2f602a833c86792ee7bb840daf662a5660` | `ae20094b04861f9952044b43c6689a7489b110264f684c3c65b7ecfee31c3ecf` | byte-different |
| source | `24687e111a6cd9f4c98506ae6b44a94037237e7fc0f94ed329578f9a70f0eb6f` | `a5ef8c885ca7d53d34fb6922ae8941daae781e52f9f18b7fd540fd5c7f1a33d6` | byte-different |

Firmware SHA-256は`5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`。8つのbinary archive manifestは`source_ref=fab5491365eae35a5adc0ce818092384eb5b9647`、source archive manifestは同じ`repository_commit`を記録する。CIではUbuntu glibc x86_64の全CTestとlibusb compatibility tests、source archive build/audit、全platform build/package/audit、候補checksum assembly、およびLinux glibc/musl x86_64・aarch64の候補CLI/IFD smokeがsuccessした。

#### pre-followup archive / run 36826506813（docs commit `8596cb8`）のarchiveとpayload比較

このfollow-up前のarchive/runは、実装commit `fab5491365eae35a5adc0ce818092384eb5b9647` に `docs/platforms/validation-results.md` だけを追加したcommit `8596cb8ee608aee23a50c4234a91a1dd93ccab7d`、およびそのworkflow run [`36826506813`](https://github.com/Khronos31/px4-userland/actions/runs/36826506813)。**これは本follow-upでREADMEを降格し本記録を追記する前の成果物であり、もはやplanned release sourceではない。** 外側`SHA256SUMS`のSHA-256は`cac67b8dbfed1a51af4d0eb9f1b0d8cb59f164ba634daab8e29ea8b97a94837e`（`sha256sum run-36826506813/SHA256SUMS`）。このrunが生成した9 archiveのSHA-256は次のとおり。

| Archive | run 36826506813 SHA-256 |
|---|---|
| linux-glibc-x86_64 | `ea5c3259c5cf255543384d389c98a0113ff0bee94324f8a2e09ac09c6a3150c6` |
| linux-musl-x86_64 | `2d329c5c71da644f279673300910a778a6c88eb55708b400c87d96dcaa8b4a79` |
| linux-glibc-aarch64 | `7cdaab75e52573b906c42ec35f129cf217ace19b4f43bc1a25b22c9586bd345d` |
| linux-musl-aarch64 | `65398881e96b66bc44a908a21b57931665d33f937dc896bb1331cf260ba2a8b2` |
| darwin-arm64 | `5e98c4af8832cf0140f2808cdd80bcf8e01d2e17dad665840d7bf5af494d332c` |
| android-aarch64 | `5795e99b4da1a9ad729a3e8f764886742916bfaf4454698125e64923b1a876cd` |
| android-armv7a | `a4f2a4d620d7f47854d69fd5798f26b577c6bf26e42522e636e3127e21a544e7` |
| android-x86_64 | `629a84001927ff81c0c0bf1ed90662b08398d06c5621cf0a45e5b7993bf57cc7` |
| source | `5b8c8d30c64776c913d575cf81ac38c638f93b3d17c12f629f0579def51ff387` |

run 36826506813の9 archiveをすべて展開し、run 36811522001と再帰比較した。7つのbinary archive（Linux glibc/musl x86_64・aarch64、Android 3 ABI）は全payloadがbyte-identicalで、差異は各`manifest.json`の`source_ref`と内側`SHA256SUMS`だけである。**実機試験で使ったLinux glibc/musl x86_64の`px4d`/`px4-ts`/`px4ctl`とIFDはrun 36826506813でもpayload-identical**（glibc `px4d` `8d87e93f1e04f60820fde2ee0adb69b3a9329436ecc20994f949e35519ab6cfd`、`px4-ts` `dfef3b536b12b4ba8add1ee3c7d0d1cdcd02f0252604ea3e5ce59255cdeb1c29`、`px4ctl` `fba496e187cb5c8a3589885614acfc268f27169c684a6d0ba643249e7a31fe7c`、IFD `acdaa868327b5cf4d3e8343d10412e6899ed8dfc46be164696b0181b740d65d5`。muslも同様）。source archiveは`repository/docs/platforms/validation-results.md`・`BUILD-RELINK.md`・`source-manifest.json`（記録するcommit/tree）と`SHA256SUMS`だけが異なる。

darwin-arm64の`px4d`だけはバイナリ自体が異なる。`cmp -l`の差は48バイトで、先頭付近のMach-O `LC_UUID` 16バイト（1-based 1609–1624、`553c0ee3…` → `b22ec010…`）と、末尾のembedded code signature領域32バイト（1-based 595650–595681）である。`px4-ts`/`px4ctl`はdarwinでもbyte-identical。darwin archiveの`evidence/binary-audit.json`も`px4d`のSHA-256を記録しているため、その該当1フィールド分だけ両runで異なる。このUUIDと署名領域はbuildごとに変わりうるため、darwin archiveのSHA-256は両runで異なる。独立したclean rebuildによる再生成比較は行っておらず、**byte-identicalとも再現性確認済みとも扱わない**。run 36811522001のdarwin archiveを実機試験に使っていないため、darwinの実機claimに与える影響はないが、run 36826506813の最終archiveそのものは未canaryである。

run 36826506813の9 archiveはcommit `8596cb8`時点で生成されており、同梱の`README.md`は本follow-upのTermux×PX-Q3U4降格を含まない（降格前の`検証済み`行を保持する）。本記録の今回追記も`8596cb8`には含まれない。**公開archiveのREADME/support表示とrepoの表示を一致させる（SPEC 10.5-8）には、今回のdocs変更をcommitしたうえでrelease-candidate workflowを再実行し、9 SHA表とpayload比較を更新する必要がある。** 再実行するまでrun 36826506813のarchiveをplanned release sourceとして扱わない。

### Claim別判定と実機結果

| Claim / 機能 | 状態 | 根拠 |
|---|---|---|
| `--list` / `--list-json`でのenclosure、USB位置、receiver、LNB対応表示 | 今回再検証 | 上記同時接続実機出力。M1UR/S1UR serial衝突、Q3U4の2内部device grouping、receiverごとの`lnb_15v_supported`を確認。 |
| M1UR/S1UR同一serialの曖昧な`--device`指定拒否 | 今回再検証 | 同時接続実機で終了コード2、2候補を表示、新規runtime pathなし。 |
| PX-M1UR/PX-S1UR Linux x86_64 native（AnduinOS）通常single-device tuner/card/native PC/SC claims | 継承 | baseline commit `2f555ff`のprofile別hardware evidenceを、上記single-bridge identity/transport/IPC/IFD hunk分析に基づき継承。candidateでは同一serial列挙・曖昧拒否・M1UR 15V要求拒否を実機確認。衝突中の`--usb-path`/`--instance`個別起動は未認定。 |
| PX-M1UR/PX-S1UR HAOS SCS Debian/glibc x86_64 通常single-device tuner/card/native PC/SC claims | 継承 | baseline commit `2f555ff`のprofile別hardware evidenceを、上記single-bridge identity/transport/IPC/IFD hunk分析に基づき継承。candidateでは同一serial列挙・曖昧拒否・M1UR 15V要求拒否を実機確認。衝突中の`--usb-path`/`--instance`個別起動は未認定。 |
| DTV02A-5TS-P / PX-MLT5PE Linux x86_64 native tuner/card claims | 継承 | 既存のPR #5 hardware evidenceを、single-bridge profileのserial/model groupingとunique-device selectionが同一observationを選ぶこと、およびMLT5の15V/GPIO条件が維持されるhunk分析に基づき継承。candidateの同profile実機試験は行っていない。 |
| PX-Q3U4 SCS native/glibc 8 receiver T/S mixed load | 今回再検証 | Candidate archive SHA `2627d888...c42cc775`。E01、2026-10-01 06:11:46–06:24:43 UTC。`/config/.work/px4-userland/v0.1.9-candidate/scs-runner.sh`をlive/default modeで実行し、daemonは`px4d --device 00001205000960 --usb-path 1-2.3.1 --usb-path 1-2.3.2 --instance v019-q3u4-canary`で起動、`px4-ts`/`px4ctl`も`--instance`で接続した。8 receiver overlap 603秒、status snapshot 51回（cycle1）+1回（cycle2）すべてready/streaming、APDU batch 52件（cycle1 51+cycle2 1、各transmit-count=10）すべてSW 90:00、cycle2の8/8 stop/reopen clean、終了後process/runtime残留なし。receiver 0–6は全counter 0。RX7値は下記。実行ログ: `/config/.work/px4-userland/v0.1.9-candidate/q3u4-glibc-results/20261001061146/`. |
| PX-Q3U4 AnduinOS/glibc E03 8 receiver short matrix | 今回再検証 | Exact candidate archive SHA `1ada505e9b0ca7071226ce32821862cdd131d5f4f7dc5d68d4f38d24ed9af80b`。列挙、card hotplug/APDU、30秒8 receiver混在受信、USB切断・再接続後のdaemon再起動、APDUを確認。再接続後receiver 0–6全counter 0、receiver 7はexit 8（TEI 10,909・continuity 574、sync/queue/USB error 0）。終了後process/runtime残留なし。fresh同時8受信`px4_drv`比較はTEI 11,035・continuity 657でcandidate値が両方とも超えず。記録は上記E03詳細および`/config/.work/px4-userland/e03-results/`。 |
| PX-Q3U4 HAOS Alpine/musl 8 receiver T/S mixed load | 今回再検証 | Candidate archive SHA `b769cd41...e6fba644`。E02、2026-10-01 05:50:59–06:01:39 UTC。600秒、RX0–7すべてsync/TEI/continuity/queue/USB error 0、status 88/88、card/APDU/PC/SC成功、終了時residualなし。別10秒stop/reopen smokeで8/8 clean。candidate CLI hashesとmanifest source_refを検証し、試験後にadd-onを停止、試験optionsとstage済み候補archiveを開始前の値へ復元。実行ログ: `/addon_configs/local_userland_stable_alpine_test/results/20261001T055059Z/`および`20261001T060233Z/`. |
| Q3U4 receiver 7 burstと`px4_drv` reference comparison | 今回再検証 | E03 AnduinOS同一個体・同firmware・同アンテナ/電源・527143 kHz・同時8受信で30秒比較。referenceはTEI 11,035 / continuity 657、candidateはTEI 10,909 / continuity 574、どちらもsync/queue/USB error 0。candidateは両counterでreference値を超えず、SPEC 10.2.6aを満たした。参照helper/module provenanceとlogsは上記E03記録に記載。 |
| M1UR same-lease T/S retune | 未認定 | 2026-10-01 06:05:45 UTC、S1URを外したPX-M1URでT→S→T→S→Tの5 intervalがlockし、`retune.log`でpacket/byte/counter一致・TS/USB error 0、`result.txt`でretune/daemon exit 0を記録した。ただし**使用したarchive/binaryのSHA-256とretune toolのsource/binary SHA-256・build条件は`m1ur-retune-results/20261001T060545Z/`に保存されていない**（`px4d.log`のendpoint名`v019-m1ur-retune`と`retune.log`のみ）。該当transcriptにもrun時のarchive/tool指定を直接示す記録は見当たらず、`/config/.work/px4-m1ur-s1ur/retune-tool/`の候補tool（source `d2dcc7ce…`、build `f36d3661…`、build-candidate `24f059f5…`）が本runで使われたことも一次証拠では結び付かない。SPEC 10.2.8のartifact軸を満たさないため未認定とし、provenanceが記録されるまで`今回再検証`にしない。 |
| `--usb-path` + `--instance` の選択経路 | 未認定 | Q3U4 topologyでは、E01採用run（`20261001061146`）と失敗試行（`20261001053440`）が`scs-runner.sh`経由で`px4d --device 00001205000960 --usb-path 1-2.3.1 --usb-path 1-2.3.2 --instance v019-q3u4-canary`を起動し、両bridgeを指定位置でopen・claimして8 receiver受信（603秒+cycle2 45秒）を行い、`px4-ts`/`px4ctl`も`--instance`で接続した。したがって**Q3U4での`--usb-path`(×2)+`--instance`によるopen・claim・受信とclient`--instance`はexact candidateで実機確認済み**。未認定の範囲は、M1UR/S1UR重複時の`--usb-path`選択と同時運用、serial名/TOKEN名の名前空間排他（混在daemon）、PC/SC `instance=`、Termux `--instance`に限る（offline/CI試験のみ）。SPEC 10.4末段・10.5-5。 |
| macOS arm64 PX-Q3U4 claim | 今回再検証 | E04のexact candidateでtuner/card/native PC/SC featureを実機確認し、receiver 7 fresh reference条件も満たした。詳細はE04記録を参照。 |
| macOS arm64 PX-M1UR / PX-S1UR hardware claim | 未認定 | E04では同時接続時のlist/JSON、serial衝突表示と曖昧`--device`拒否を確認した。両機種の受信・card・native adapter機能はこのcandidateで認定していない。 |
| Android Termux aarch64（E05）PX-Q3U4 claim | 未認定 | Exact candidateの2-FD launcher、card hotplug、USB切断後のlauncher再起動、8 receiver、受信中APDUは実機確認済み。receiver 7 burstを記録した。保存recordではE05 runとfresh `px4_drv` referenceの条件一致を確認できないため、SPEC 10.2.6a comparatorを満たしたとは扱わない。 |
| Android Termux armv7a（E06）PX-Q3U4 claim | 未認定 | Exact candidateの2-FD launcher、card/USB hotplug、8 receiver受信、受信中APDUは実機確認済み。最初のreceiver 7 runはTEI 11,044でE03 referenceを上回り、再接続後runはTEI 10,960・continuity 629だった。全fresh comparator条件を揃えた判定が記録されていないためsupport claimは保留する。 |
| Android Termux x86_64 / Bliss OS（E07）PX-Q3U4 claim | 未認定 | Exact candidateの2-FD launcher、8 receiver、card hotplug、USB detach/reconnect後の復旧とAPDUは実機確認済み。receiver 7 burstを記録したが、保存recordでSPEC 10.2.6aのfresh reference条件を照合できないためsupport claimは保留する。 |
| LNB 15V実給電 | 未認定 | 今回のE01/E02では15Vを有効にしていない。`--list`の`lnb_15v_supported`表示はprofile capabilityであり給電試験ではない。Q3U4のLNB経路はhunk分析で実効的に非影響と判定するが、実給電は今回未実施。SPEC 10.2-17の代表負荷時給電はStableのブロッカーではない。 |
| Android ad-hoc APK / Windows / FreeBSD | 対象外 | 現行SPECの製品範囲とrelease gateによる。 |

macOS arm64のE04 PX-Q3U4 hardware claimは、同run `36878304743`のexact `darwin-arm64` candidateによる実機試験後に今回再検証へ更新した（E04記録参照）。PX-M1UR/PX-S1URはlist・serial ambiguityの範囲だけ確認済みで、受信・card・native adapter claimは未認定のままとする。

### Q3U4影響範囲・soak判定

Candidate差分をhunk単位で確認した。`identity.cpp`はQ3U4の15桁serial末尾1/2からbridge slotを得る規則を維持し、同一base serial・同一modelの2 bridge groupingも維持する。新しいcandidate indexは従来slot別exact-serial searchと同じobservationを指す。Q3U4のbase serialはsingle-device M1UR/S1URの15桁serialと異なり、このcandidateではuniqueである。`libusb_transport.cpp`の変更はnative/Fd acquisition時のdevice selection plumb-throughであり、通常serial指定の有効なQ3U4 pairでは従来と同じUSB handleを選択する。USB interface open/claim、endpoint transfer、TS capture、demux、queue/counter処理および`q3u4_stream.cpp`は変更されていない。`px4_ts_core.cpp`の変更はIPC endpoint instance key選択、`it930x.cpp`変更はsingle-receiver profileのGPIO条件であり、Q3U4の条件は従来どおり有効。

このcall-path証拠に加え、E03でSPEC 10.2.6aのfresh `px4_drv`比較を実施した。candidateのreceiver 7 counterは30秒同時8受信でreference値を超えず、既知burstとして記録する。今回soakはユーザー決定により実施しない。`it930x.cpp`のGPIO 11条件は`layout == single_receiver`から`!supports_lnb_15v`へ変わったが、Q3U4では`supports_lnb_15v=true`のため従来と同じくGPIO 11を設定・検証する。`px4d.cpp`のLNB許可値も`allow_lnb_power && supports_lnb_15v`となり、Q3U4では従来と同じ。

### v0.1.9 exact-candidate hardware runとcanary選定理由

今回のcandidate差分は`identity.cpp`、`libusb_transport.cpp`、`posix_ipc.cpp`、`pcsc_ifd_adapter.cpp`、`it930x.cpp`、`px4d.cpp`、`px4-ts`/`px4ctl`および`px4-termux`ランチャーに及ぶ共通実装の変更で、v0.1.8 release archiveに対して9 archiveすべてがbyte-differentである（SPEC 10.5-3の「影響するartifactがある」場合に該当）。

exact candidateによる**Q3U4 8 receiver混在負荷run**は次の2件で、環境IDは`docs/release-validation.md` §0のcanonical IDである。この2件の他に、M1UR same-lease retune run（06:05:45 UTC、artifact provenance未保存）、失敗したlive試行`20261001053440`、15:33 JSTの同時接続`--list`/`--list-json`・曖昧拒否確認がある。

| 環境ID | runtime/access path | host | device | archive SHA-256 | UTC | 内容 |
|---|---|---|---|---|---|---|
| E01 | HAOS x86_64 Debian/glibc SCS（container root） | Linux 6.18.52-haos x86_64 | PX-Q3U4 `00001205000960` | `2627d888...c42cc775`（linux-glibc-x86_64） | 2026-10-01 06:11:46–06:24:43 | `scs-runner.sh`が`px4d --usb-path 1-2.3.1 --usb-path 1-2.3.2 --instance v019-q3u4-canary`で起動。8 receiver T/S mixed load 603秒、status snapshot 51回（cycle1）+1回（cycle2）すべてready/streaming、APDU batch 52件（各transmit-count=10）すべてSW 90:00、cycle2 stop/reopen 8/8、終了後残留なし |
| E02 | HAOS x86_64 Alpine/musl add-on（Supervisor） | Linux 6.18.52-haos x86_64（musl 1.2.5） | PX-Q3U4 `00001205000960` | `b769cd41...e6fba644`（linux-musl-x86_64） | 2026-10-01 05:50:59–06:01:39 | Q3U4 8 receiver T/S mixed load 600秒、RX0–7全error 0、status 88/88、card/PC/SC成功、終了後残留なし（daemonはserial指定、PC/SCも`device=`指定） |

選定理由: candidateが共通実装を変更し影響artifactがあるため、SPEC 10.5-3と`docs/release-validation.md` §3.2は最も複雑なtopology（PX-Q3U4）を選び、該当OS/access pathをcanonical固定順E03、E01、E02、E04…の先頭で絞るよう定める。一方、§4は「共通実装を変更したがQ3U4非影響を立証」する場合に、exact candidateでSCS native/glibcとHAOS Alpine/muslの各pathのQ3U4 8 receiver ISDB-T/S混在受信を10分以上行うことを要求する。今回のE01/E02はこの短時間回帰を両Linux runtimeで満たしたものである。固定順先頭のE03（AnduinOS x86_64）でのcanary、およびSPEC 10.5-3が求める「公開するfinal candidate artifactそのもの」を使ったcanary（10.5-3のsame-lease retune、10.5-4のUSB detach/reconnectを含む）は**未完了**である。E01/E02をE03 canaryまたはfinal-candidate canaryの完了として扱わない。

E01/E02の詳細はClaim別表の該当行と`/config/.work/px4-userland/v0.1.9-candidate/q3u4-glibc-results/20261001061146/`、`/addon_configs/local_userland_stable_alpine_test/results/20261001T055059Z/`・`20261001T060233Z/`に保存した。

### 失敗したlive試行 20261001053440

2026-10-01 05:34:40Z–05:47:36Zに`scs-runner.sh`をlive modeで実行した試行。runnerの最終判定は`failed`で、cycle1（`monitoring complete for cycle1: 601 s`）の後に`cycle cycle1 rx7: missing or non-positive packets`と`receiver 7 exited with code 8 (TS integrity)`を記録した。保存されているcycle1 summaryではreceiver 0–6はexit 0・全error counter 0、receiver 7は`packets=? bytes=? sync=? tei=? continuity=? queue=? usb=?`でcounterが欠落している。cycle2（45秒stop/reopen）は8 receiverすべてexit 0で、`px4-ts-rx7.log`に残るのは45秒・516,449 packets・error 0の1行だけである。**cycle1で欠落したreceiver 7のcounter値と、その欠落原因は保存logからは特定できず、推定で補わない。** この試行は失敗した実機試行として保持し、passへ書き換えない。

この試行のcleanupでは、px4dへのSIGINT後にwait timeout、SIGKILL fallback、`runtime directory not empty after cleanup`を記録した。原因は切り分けていない。採用run（E01 `20261001061146`）とは別試行である。

## 2026-09-30 PX-M1UR / PX-S1UR 候補版のクロスプラットフォーム・アクセスパス実機試験

CIの**push-run候補** `2f555ff0542c7a36fb2565b64ebc0703f44a0931`（firmware SHA-256: `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`）を用い、
HAOS Studio Code Server（Debian 13 glibc x86_64）、HAOS Supervisor管理Alpine add-on（musl x86_64）、
およびM2 Mac mini（macOS 26.6.2 arm64）で実機試験を実施した。
各pathで使用した候補アーカイブのSHA-256は以下のとおりである（ファイル名の`0.1.7`は候補ビルドのラベルであり公開リリースではない）。
- Linux glibc x86_64: `ae2ac0c3c88dc95a948929784bb2d6813522cf1526d21383a7a3339f1c6da8eb` (`px4-userland-0.1.7-linux-glibc-x86_64.tar.gz`)
- Linux musl x86_64: `e283d88f1a529a2d9aa76043d08a0563e2ba5acd6dc7535b054002d29a95595d` (`px4-userland-0.1.7-linux-musl-x86_64.tar.gz` staged)
- macOS arm64: `333d6cbaa4d5825ba067122c7bbceff6b07e168d5a82fa5ab75c04dfadb3199f` (`px4-userland-0.1.7-darwin-arm64.tar.gz`)

両機種ともUSB serialは`000000000000001`で、PX-M1UR（`0511:0854`、1 receiver ISDB-T/S）、PX-S1UR（`0511:0855`、1 receiver ISDB-T専用）を個別のUSB IDで識別し、1台ずつ接続した。
Android Termuxの1-FD経路は後掲の別表で記録する。Android APKおよびWindowsは本試験の対象外である。

| Model / 環境 / access path | 30分連続受信 + PC/SC併走 | 短時間受信・カード・追加確認 | USB切断/再接続・カード抜去/再挿入 |
|---|---|---|---|
| PX-M1UR<br>HAOS SCS<br>Debian 13 glibc x86_64 | ISDB-T 527143 kHz 1800秒。20,666,794 packets / 3,885,357,272 bytes。sync/TEI/continuity/queue-drop/USB errors 0、empty intervals 1,563,574、exit 0。受信中に実PC/SC `scriptor` APDU 290/290成功（全てSW 90 00）。中間statusはready/streaming、errors 0。 | ISDB-T 527143 kHz 10秒（115,834 packets / 21,776,792 bytes）、ISDB-S 1049480 kHz slot 0 LNB 0V 10秒（116,668 packets / 21,933,584 bytes）、全エラー0、exit 0。直接カードstatus/ATR、APDU 10回成功。 | カード抜去時`card-present=no`、generation 1→2、APDUは`NO_CARD`（exit 9）、`pcsc_scan`でCard removed。再挿入後generation 3、直接ATR/reset/APDU 10回、PC/SC reset/APDU成功、再挿入後T 5秒（57,901 packets）エラー0。USB切断時旧daemon生存も`DISCONNECTED`（exit 7）、再接続時USBデバイス番号024で再列挙されても旧daemonは自動復帰せず。旧daemon停止・同一候補daemon新規起動で復帰確認（T 10秒 115,834 packets、S 0V 10秒 160,718 packets、全エラー0）。 |
| PX-S1UR<br>HAOS SCS<br>Debian 13 glibc x86_64 | ISDB-T 527143 kHz 1800秒。20,667,610 packets / 3,885,510,680 bytes。sync/TEI/continuity/queue-drop/USB errors 0、empty intervals 1,568,298、exit 0。受信中に実PC/SC `scriptor` APDU 70/70成功（全てSW 90 00）。中間statusはready/streaming、errors 0。 | 直接カードstatus/ATR、APDU 10回成功。実PC/SC `scriptor`でS1URリーダー選択、resetおよびAPDU SW 90 00成功。同一daemon上でISDB-T 527143 kHz 5秒受信を2回実施し（58,714 packets、57,899 packets、全エラー0、exit 0）、clean stop/reopenを確認。初回の試験コマンドは`--group`フラグ欠落による`INVALID_ARGUMENT`であり、修正後の実行で合格（運用上の指定漏れであり製品不具合ではない）。 | カード抜去時`card-present=no`、generation 1→2、直接`card-atr`は`NO_CARD`（exit 9）、`pcsc_scan`でCard removed。再挿入後generation 3、直接ATR/reset/APDU 10/10（SW 90 00）、実PC/SC reset/APDU（SW 90 00）成功、再挿入後T 5秒（57,898 packets / 10,884,824 bytes）全エラー0。USB切断時旧daemon・`pcscd`生存も`DISCONNECTED`（exit 7）、`pcsc_scan`でCard removed。再接続時USBデバイス番号026で再列挙されても旧daemonは`DISCONNECTED`（exit 7）のまま自動復帰せず。旧daemon停止・同一候補daemon新規起動でready/free復帰、実PC/SC reset/APDU（SW 90 00）および直接APDU 10/10成功。再起動後T 10秒（115,833 packets / 21,776,604 bytes、sync/TEI/continuity/queue-drop/USB errors 0、exit 0）で復旧確認（restart-based recovery）。 |
| PX-M1UR<br>HAOS Supervisor<br>Alpine musl x86_64 | 30分soakは未実施。 | ISDB-T 527143 kHz 約32秒（368,794 packets / 69,333,272 bytes）、ISDB-S 1318000 kHz slot 0 LNB 0V 約32秒（510,788 packets / 96,028,144 bytes）、全エラー0、exit 0。衛星15V要求は`UNSUPPORTED`（exit 3）で拒否。直接カードATR/reset/APDU 10回、PC/SC `opensc-tool` APDU SW 90 00成功。 | 物理ホットプラグ（カード抜去・USB抜差し）は未実施。 |
| PX-S1UR<br>HAOS Supervisor<br>Alpine musl x86_64 | 30分soakは未実施。 | ISDB-T 527143 kHz 約32秒（367,978 packets / 69,179,864 bytes）、全エラー0、exit 0。直接カードATR/reset/APDU 10回、PC/SC `opensc-tool` APDU SW 90 00成功。初回はrunnerがISDB-Tへ衛星専用の`--lnb-voltage 0`を渡して失敗し、修正後の再実行で合格。 | 物理ホットプラグ（カード抜去・USB抜差し）は未実施。 |
| PX-M1UR<br>M2 Mac mini<br>macOS 26.6.2 arm64 | 30分soakは未実施。 | ISDB-T 527143 kHz 10秒（116,651 packets / 21,930,388 bytes）、ISDB-S 1318000 kHz slot 0 LNB 0V 10秒（159,909 packets / 30,062,892 bytes）、全エラー0、exit 0。同一daemon上でISDB-T 527143 kHz 5秒受信を2回実施（各59,531 packets、全エラー0、exit 0）しclean stop/reopenを確認。Homebrew `pcsc-lite`実consumerで13バイトATR、reset、APDU 10/10成功（SW 90 00）。 | カード抜去時`card-present=no`、generation 1→2、直接`card-atr`は`NO_CARD`（exit 9）、Mac PC/SC consumer接続失敗（exit 1）。再挿入後generation 3、直接ATR/reset/APDU 10/10（SW 90 00）、Mac native PC/SC reset/APDU 10/10（SW 90 00）成功、再挿入後T 5秒（58,714 packets / 11,038,232 bytes、全エラー0、exit 0）。USB切断時旧daemonは`DISCONNECTED`となり自動復帰せず、再接続後`px4d --list`で認識、同一候補daemon再起動で復旧確認（先行試験の再起動後T 10秒 116,651 packets、S 10秒 159,909 packets、全エラー0、restart-based recovery有効）。 |
| PX-S1UR<br>M2 Mac mini<br>macOS 26.6.2 arm64 | ISDB-T 527143 kHz 1800秒。20,667,610 packets / 3,885,510,680 bytes。sync/TEI/continuity/queue-drop/USB errors 0、empty intervals 1,439,275、exit 0。受信中に実PC/SC consumer APDU 290/290成功（全てSW 90 00）。中間statusはready/streaming、errors 0。 | ISDB-T 527143 kHz 10秒（116,650 packets / 21,930,200 bytes、全エラー0）。直接カードstatus/ATR、APDU 10回成功。実PC/SC consumerでATR取得、reset、受信中APDU 10/10成功。macOS上で同一daemonのstop/reopenは未実施。 | カード抜去時`card-present=no`、generation 1→2、APDUは`NO_CARD`（exit 9）、PC/SC接続失敗（exit 1）。再挿入後generation 3、直接ATR/reset/APDU 10回、PC/SC ATR/reset/APDU 10/10成功、再挿入後T 5秒（57,901 packets）エラー0。USB切断時旧daemon生存も`DISCONNECTED`（exit 7）、PC/SC接続失敗（exit 1）。再接続時新USBインスタンス認識も旧daemonは自動復帰せず。旧daemon停止・同一候補daemon新規起動で復帰確認（再起動後T 10秒 115,835 packets、全エラー0）。 |

### 補足事項・運用上の確認事実

- **USB切断・再接続時の復旧挙動**: 物理切断を行ったnative pathでは、USB切断時に旧daemonプロセスは終了せず`DISCONNECTED`を返し続けた。USB再接続後も旧daemonが同一プロセス内で自動再接続することは観測されず、旧daemonを終了して同一候補版バイナリを再起動することで正常復帰を確認した（restart-based recovery）。Alpine add-onでは物理切断を試しておらず、同一プロセス内での自動再接続も立証していない。
- **未検証項目とサポート主張の扱い**: 本記録はCI push-run候補バイナリによる個別アクセスパスの実機試験結果であり、公開版v0.1.7や最終リリース成果物の認定ではない。SPEC 10.3に基づき、各環境で実際に確認された事実のみを記録し、未試験項目（S1UR Latitudeでの30分PC/SC併走、M1UR macOSでの30分soak、Alpineでの30分soakや物理ホットプラグなど）への推論による`runtime-supported`の昇格は行わず、判定を保留（pending）とする。
- **30分試験の原始出力**: 保存先はHAOSのCodexセッショントランスクリプト `/config/.tools/codex-home/sessions/2026/09/26/rollout-2026-09-26T19-34-40-01a0dd48-080b-7171-8e78-91710ff6cb17.jsonl`。候補版の`px4-ts`終了出力は、PX-M1UR Latitude ISDB-Tがordinal 19071（2026-09-29 10:43:05 UTC、`empty-intervals=1592300`）、PX-S1UR Latitudeが20543（11:35:05 UTC、`1591871`）、PX-M1UR Latitude ISDB-Sが23379（13:40:08 UTC）、PX-S1UR HAOS SCSが23619（13:48:31 UTC、`1568298`）、PX-M1UR HAOS SCSが27254（16:29:48 UTC）、PX-S1UR macOSが27377（16:36:12 UTC、`1439275`、`PX4_TS_EXIT=0`）に残る。独立した実行で同一packet数となったS1UR3件の理由は未解明であり、パケット内容が同一または異なることの証拠にはしない。各試験のモデル・host・時刻・候補archiveの対応は上表と同セッション内の起動・状態・PC/SC出力に記録されている。

### Android Termux（正式launcherの1-FD経路）

同じCI push-run候補 `2f555ff0542c7a36fb2565b64ebc0703f44a0931` と上記SHA-256のfirmwareを使用し、各機種を個別に接続した。Android用アーカイブのSHA-256はaarch64が`84111dfd45833cd6e8157b7a593d74557eb5fdd90c108fa10f93f98ba4537970`、armv7aが`689238dd1431b3a2974b6a86d25f240d2bfc8dfe6f6e0456b981e2e49c8a32b1`、x86_64が`a3aa155cdb3d3a6b1e543d3eb7fd4295fa4c078cff36fdc2381c58ccc91625ca`であり、それぞれ外側と展開後のチェックサムを照合した。いずれも`px4-termux`へUSB FDを1つだけ渡し、LNB 15Vは要求していない。

| 実機 / Termux | PX-M1UR `0511:0854` | PX-S1UR `0511:0855` |
|---|---|---|
| Pixel 9a / Android 17 / aarch64 / Termux 0.118.3 | 1 receiver ISDB-T/S。地デジ10秒115,835 packets、衛星0V 10秒159,907 packets、再地デジ15秒172,954 packets。受信中APDU 10回完走（最終SW 90:00）。 | 1 receiver ISDB-Tのみ。地デジ10秒115,835 packets、再受信15秒173,770 packets。衛星要求は`INVALID_ARGUMENT`（0 packets）。受信中APDU 10回完走（最終SW 90:00）。 |
| Google TV Streamer / Android 14 / armv7a / Termux 0.119.0-beta.3 | 1 receiver ISDB-T/S。地デジ10秒116,650 packets、衛星0V 10秒161,540 packets、再地デジ15秒172,955 packets。受信中APDU 10回完走（最終SW 90:00）。 | 1 receiver ISDB-Tのみ。地デジ10秒116,650 packets、再受信15秒173,771 packets。衛星要求は`INVALID_ARGUMENT`（0 packets）。受信中APDU 10回完走（最終SW 90:00）。 |
| IP3 GT1 / Bliss OS Android 13 / x86_64 / Termux 0.118.3 | 1 receiver ISDB-T/S。地デジ10秒115,835 packets、衛星0V 10秒159,908 packets、再地デジ15秒173,771 packets。受信中APDU 10回完走（最終SW 90:00）。初回の機器初期化のみ`TIMEOUT`（exit 5）で、USB許可を再要求した2回目と、再要求しない3回目は起動成功。その後の物理挿し直し後の初回起動も成功したが、最初の失敗原因は未特定。 | 1 receiver ISDB-Tのみ。初回起動成功。地デジ10秒115,834 packets、再受信15秒172,954 packets。衛星要求は`INVALID_ARGUMENT`（0 packets）。受信中APDU 10回完走（最終SW 90:00）。 |

各受信の`bytes = packets × 188`、sync/TEI/continuity/queue-drop/USB errorsはすべて0で、captureはexit 0。各pathで直接IPCのカードATR・reset・APDU反復、受信中のカード操作、停止後の再受信、launcher終了後のprocessとIPC socketの残留なしを確認した。Bliss OSのM1UR初回起動失敗は合格結果に埋めず、初回起動の信頼性は未確定とする。Termux上でカード抜去/再挿入および受信中のUSB切断/再接続は試験していないため、SPEC 10.3の`card-core-hardware-verified`・`tuner-hardware-verified`・`runtime-supported`の全条件を満たしたとの主張はしない。Android APK経路は別製品側で確認する予定であり、本記録に含めない。

### Q3U4 exact-candidate short regression for shared-source impact

Candidate `2f555ff0542c7a36fb2565b64ebc0703f44a0931`（firmware SHA-256 `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`）について、single-receiver向け変更がQ3U4へ影響しないことを確かめるため、同一PX-Q3U4 `00001205000960`でSCS native/glibcとHAOS Alpine/muslの8 receiver混在負荷を各10分以上実施した。SCS archive SHA-256は`ae2ac0c3c88dc95a948929784bb2d6813522cf1526d21383a7a3339f1c6da8eb`、Alpine archive SHA-256は`e283d88f1a529a2d9aa76043d08a0563e2ba5acd6dc7535b054002d29a95595d`。

Alpineでは先行するrun `20260930T041427Z`があり、px4d ready後に8つの`px4-ts`すべてが`TIMEOUT`・0 packetsで終了したため、10分回帰としては不合格であり集計に含めていない。停止後のresidualsはなく、原因は確定していない。後続の採用runは、Q3U4の15V adapterと両RF leadsを接続した後に別々に実施した。失敗runの詳細は下記実行記録を参照。

| Runtime/access path | 8 receiver T/S mixed load | status + card / PC/SC | stop/reopen、終了処理 |
|---|---|---|---|
| HAOS SCS native Debian/glibc x86_64 | receiver 0–7が638秒同時稼働。各receiverは正のTS packet/byteを出力し、receiver 0–6はsync/TEI/continuity/queue/USB error 0、exit 0。receiver 7は`STREAM_END error=0`、TEI 10,944、continuity 569、sync/queue/USB error 0、exit 8。 | status 20/20、card APDU 20/20 batch（各10回）が成功。開始時にもAPDU成功。 | 全stream停止後、receiver 2を10秒ずつ2回再openし、両方exit 0。再open後のstatus/APDUも成功。px4d、streamおよびUSB nodeの残留なし。 |
| HAOS Supervisor Alpine/musl x86_64 | 600秒の設定時間を完走。receiver 0–6は各約6.96–7.03M packets、sync/TEI/continuity/queue/USB error 0、exit 0。receiver 7は6,963,968 packets、TEI 10,972、continuity 606、sync/queue/USB error 0、exit 8相当の既知burst。runnerは`known-receiver7-burst-nonblocking`として記録し、全体`status=passed`。 | status 88/88でUSB/protocol error 0。direct APDU、PC/SC readerおよびAPDUが成功し、card sample failure 0。 | 別の10秒runで8 receiverを全てstop/reopenし、8/8 exit 0・TS/USB error 0。両runとも`residuals=none`。試験用Supervisor optionsを開始前の値へ復元し、add-onを停止。 |

SCSのreceiver 7 burstは`px4-ts`がexit 8を返すため、その実行ラッパー全体の終了値は非zeroである。既存の同一Q3U4 receiver 7参照記録（同一T22、約11k TEI）と既知の短時間burst記録に照らして保存し、無条件のclean結果とは扱わない。今回のcandidate差分では、`it930x.cpp`のGPIO変更は`BoardLayout::single_receiver`だけに適用される。`q3u4_stream.cpp`の`kQ3U4Layout`は`dual_system=false`かつ`plain_ts=false`であり、追加された`plain_ts`分岐は適用されず、従来のreceiver位置に基づくT/S判定とtagged-TS経路がそのまま使われる。single-receiver用layoutやQ3U4の既存receiver mappingは変更されていない。この正確なcall-path proofと、適正なRF構成で合格した両runtimeの短時間回帰に基づき、今回変更起因のQ3U4 2時間soakはtriggerしない。周期再認定の独立gateは引き続き適用する。実行ログは`/config/.work/px4-m1ur-s1ur/q3u4-short/EXECUTION-20260930.md`およびその記載先に保管した。

## 2026-09-29 PX-M1UR / PX-S1UR 候補版のLinux実機試験

Latitude 5300 / AnduinOS 2.0.3（Linux x86_64 / glibc 2.43、native libusb）で、
CIの**push-run候補** `2f555ff0542c7a36fb2565b64ebc0703f44a0931` を試験した。
使用した`px4-userland-0.1.7-linux-glibc-x86_64.tar.gz`のSHA-256は
`ae2ac0c3c88dc95a948929784bb2d6813522cf1526d21383a7a3339f1c6da8eb`、
manifestの`source_ref`は同じcommitである。archive名の`0.1.7`は候補ビルドのラベルであり、
公開済みv0.1.7のバイナリではない。firmwareのSHA-256は
`5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`。
両機種のUSB serialは`000000000000001`で、それぞれ別のUSB IDで識別し、1台ずつ接続した。
以下の結果はこの候補とこのLinux native pathに限る。

| Model / USB ID | 30分連続受信 | 受信・カード・USBの追加確認 | PC/SC併走10分 |
|---|---|---|---|
| PX-M1UR `0511:0854` | receiver 0、ISDB-T 527143 kHz、20,667,610 packets / 3,885,510,680 bytes。sync/TEI/continuity/queue-drop/USB errorsは全て0、exit 0。 | 1 USB / 1 receiver / ISDB-T/Sの列挙、ISDB-T/S 0V受信、同一leaseのT→S→T→S→T、15V要求のopt-inなし・あり双方で`UNSUPPORTED`、カード抜去/再挿入・ATR/reset/APDU、USB切断・再接続後にdaemon新規起動してT/Sとカードの復帰を確認。 | 527143 kHz、6,889,450 packets / 1,295,216,600 bytes。TS/USB errorsは全て0、exit 0。実PC/SC consumer `scriptor`の受信中APDU 60/60成功。 |
| PX-S1UR `0511:0855` | receiver 0、ISDB-T 527143 kHz、20,667,610 packets / 3,885,510,680 bytes。sync/TEI/continuity/queue-drop/USB errorsは全て0、exit 0。 | 1 USB / 1 receiver / ISDB-T専用の列挙、ISDB-S要求の拒否、527143→521143→527143 kHzの同一lease retuneで3区間全てlock・TS errors 0、カード抜去/再挿入・ATR/reset/APDU、USB切断・再接続後にdaemon新規起動してTとカードの復帰を確認。 | 527143 kHz、6,889,450 packets / 1,295,216,600 bytes。TS/USB errorsは全て0、exit 0。実PC/SC consumer `scriptor`の受信中APDU 60/60成功。 |

両機種ともPC/SCのリセットと反復APDUを実consumerから確認した。USB切断後の旧daemonは
アイドル時に`DISCONNECTED`を返し続けたため、停止してから同じ候補版を新規起動した。
同一プロセスでの自動再接続は立証していない。試験後はdaemonを停止し、PC/SCサービスを
試験前の停止状態へ戻し、一時reader設定を退避した。

この記録はPX-M1UR / PX-S1URについて、上記Linux native pathの
`tuner-hardware-verified`、`card-core-hardware-verified`、`native-card-adapter-verified`の
個別証拠である。一方、上記表中のPC/SC併走10分短縮は初期の試験運用であり、
現行SPEC 10.2.7・10.3の30分条件を変更・緩和しない。同日夜の追試において、PX-M1URはLatitude上で
ISDB-S 0V（1318000 kHz、slot 0）の1800秒連続受信とnative PC/SC併走（`scriptor` APDU 10×5=50/50成功、
28,619,541 packets / 5,380,473,708 bytes、TS/USB errors 0）を完走した。
一方、PX-S1URのLatitude環境自体は依然として10分native PC/SC併走（6,889,450 packets、APDU 60/60成功）に
とどまる（後続試験として別pathのHAOS SCS上でISDB-T 1800秒+native PC/SC 70/70完走を記録したが、
Latitude native pathの代替とはならない）。
したがって「両機種ともPC/SC併走が10分帯までしか観測されていない」という初期の記述は不正確であり、
M1UR（Latitudeでの衛星0V）およびS1UR（HAOS SCSでの地上波）で30分併走を確認済みである。
ただし、S1URのLatitude native path単体では30分PC/SC併走を満たしておらず、M1URのLatitude地上波におけるPC/SC併走も
10分にとどまるため（地上波30分はdirect IPCのみ）、この段階では`runtime-supported`の証拠としない。
Windows / WebTS.app、Androidその他の未試験runtimeへ外挿しない。macOSでの直接の試験結果は前掲の別pathの行に限る。候補版全体のrelease canaryとPR mergeも別gateである。

## 2026-09-29 PX-M1UR 認定途中（v0.1.7）

Latitude 5300 / AnduinOS 2.0.3（Linux x86_64 / glibc 2.43）で、PX-M1UR `0511:0854` を
v0.1.7の正式Linux glibc x86_64 archive（source commit
`b7685ad9940e278bdb0809dec4cc92247b8844ec`、archive SHA-256
`b137938e778b2dccc0b9a1f1ede14040bbc94826d187e5a424f740c4d9ca2acd`）から実測した。
この記録は認定完了や、他OS・他architectureへのサポート主張ではない。

- receiver 0のISDB-T 527143 kHzを正式`px4-ts`で単一の連続1800秒取得。20,667,610 packets、
  3,885,510,680 bytes、sync/TEI/continuity/queue-drop/USB errorsはすべて0。取得中のdirect APDUも成功した。
  これ以前の別の5分試行ではcontinuity errorが1件あり、原因未解明の失敗として保持する。
- ISDB-TとISDB-S（1318000 kHz、slot 0、LNB 0V）の短時間取得、同一leaseのT→S→T→S→T、
  card抜去・再挿入、T/S取得中のnative PC/SCによるATR・APDU・resetを確認した。
- 壁設備から分離した開放端で、`px4d --allow-lnb-power`と`px4-ts --lnb-voltage 15`を指定しても
  30秒間0Vのままであった。計器は別途乾電池で1.5Vを示した。無信号のためtune自体はtimeoutした。
  この測定はPX-M1URのLNB 15V出力を立証しない。参照ドライバでもM1URの給電callbackは無効であり、
  SPEC v0.22では15V出力を対応範囲から除外した。
- 当時のv0.1.7コードは、opt-in時にPX-M1URの15V要求を拒否せずGPIO 11を書き込むためSPEC v0.22と
  不一致だった。このv0.1.7記録だけではPX-M1URをhardware-verifiedとしない。後続candidateの修正・認定結果は
  本文冒頭の2026-09-30 candidate記録を参照する。

参照ドライバ: [Linux M1UR source](https://github.com/tsukumijima/px4_drv/blob/c995c10138368283a720cec4fcbca157ccc0dbd3/driver/m1ur_device.c)、
[WinUSB M1UR source](https://github.com/tsukumijima/px4_drv/blob/c995c10138368283a720cec4fcbca157ccc0dbd3/winusb/src/DriverHost_PX4/isdb2056_device.cpp)。

同じ参照revisionのLinux driverでは、DTV02-1T1S-U / DTV02A-1T1S-Uに対応する
[ISDB2056 / ISDB2056N](https://github.com/tsukumijima/px4_drv/blob/c995c10138368283a720cec4fcbca157ccc0dbd3/driver/isdb2056_device.c)も
LNB setterが無効で、GPIO 11初期化は無効化されている。
[S1UR / ISDBT2071](https://github.com/tsukumijima/px4_drv/blob/c995c10138368283a720cec4fcbca157ccc0dbd3/driver/s1ur_device.c)は
地上波専用でGPIO 11初期化経路を持たない。これらは参照実装との一致を示すだけで、手元にない機種の
実機電圧・受信動作を確認したことにはならない。

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
| EPSON Endeavor NJ1000 / Debian 12 i386（32-bit / glibc / EHCI） | Stable v0.1.3 source archive | 合格。PC/SC IFD込みnative Release build（98/98 targets）、CTest 7/7、PC/SC smoke、8受信機同時30分soak（r7既知バースト再現、PC/SC・direct APDU完走）。8受信機とPC/SC consumer同時稼働下の物理切断（全受信機・px4d有限終了 exit 7、PC/SC切断観測および明示cleanup、残留0）、およびOS再起動なしの再接続（60秒runでr0〜r6 clean、r7既知バーストexit 8を含めハーネス既定規則でoverall pass、PC/SC・direct APDU成功、過去runの全8 clean復帰も併記保持）を確認。 | 実機操作はroot/sudoで実施。詳細は [Debian 12 i386での検証結果](debian-i686.md) を参照。 |
| Latitude 5300 / AnduinOS（localhost usbip / VHCI） | Stable v0.1.3 x86_64 glibc archive | 合格（初回strict fail保持、再試BS15_0完走）。VHCI側2ノードを`--fd`指定し8受信機30分soak、direct APDU完走。 | localhost/VHCI経路の実績。LAN経由は再検証対象。詳細は [localhost usbipでの検証結果](usbip-localhost.md) を参照。 |
| Latitude 5300 / AnduinOS（AppArmor enforce） | Stable v0.1.3 x86_64 glibc archive | 合格。USB拒否gate、complain、enforce 60秒、enforce 30分（先行fail 2回を経て再接続後3回目で8 receiver全エラー0完走）、拘束`px4d`経由でのIFD/`pcscd` consumer動作（APDU 30/30 SW9000）、受信中物理切断（全receiver exit 7、有限停止）、OS再起動なしの再接続復帰（8 receiver 60秒・PC/SC APDU 6/6 pass）、cleanup passを確認。 | profileは利用者側で用意する。`pcscd`の実labelはunconfinedであり、`pcscd`専用profileは検証対象外。詳細は [AppArmorで実行する際の注意](apparmor.md) を参照。 |

## 2026-09-10 Android正式launcher実機検証（レビュー前候補archive）

対象branch headは`fa45792787905d3a86a8cab0bbc9ab7c860c665e`。以下は、40秒猶予およびbytecode監査の修正前に作成した、
正式launcher実機検証済みのレビュー前候補archiveによる実機結果であり、現在の最終候補archiveではない。

| 環境 | レビュー前候補archive SHA-256 | 確認内容 |
| --- | --- | --- |
| Pixel 9a（Android 17 / aarch64 / Bionic、Termux 0.118.3） | `737f2b42d9b02be9763ad186dee8129a17121fbc00c753d4e8e3bdf9516b6723` | 正式`px4-termux`で内蔵カード、地上波、衛星、TERM/INT/HUP、第2USB取得失敗、30分8 receiver、APDU 30/30、物理切断exit 7を確認。receiver 0〜6は全エラー0、receiver 7は既知burstのみ。全processとIPC endpointの残留なし、再接続後も正常。 |
| Google TV Streamer（Android 14 / API 34 / armv7a / Bionic、Termux 0.119.0-beta.3） | `d7da07acea5f676f698a86e92c0b80e87c1dbe2aca8b01414cc5db8cd24f302b` | 正式`px4-termux`で内蔵カード、地上波、衛星、TERM/INT/HUP、第2USB取得失敗、30分8 receiver、APDU 30/30、物理切断exit 7を確認。receiver 0〜6は全エラー0、receiver 7は既知burst（TEI 10,949、continuity 627、queue/USB error 0）のみ。全processとIPC endpointの残留なし、再接続後も正常。 |
| IP3 GT1 / Bliss OS（Android 13 / x86_64 / Bionic、Termux 0.118.3） | `7561b51c0b6e934b044982eac0539b5a3de556c331e59b9c6b6f062d3b8f9222` | 正式`px4-termux`で30分8 receiverを実施し、8/8 exit 0、sync/TEI/continuity/queue/USB errorを全て0で確認。内蔵カードAPDU 30/30、TERM/INT/HUP、第2USB取得失敗、物理切断exit 7、全processとIPC endpointの残留なし、再接続後のカードAPDU 10/10・地上波・衛星を確認。 |

Bliss OSではバックグラウンド時にTermux UID全体が凍結し、`termux-wake-lock`も同環境で`Bad system call`となった。検証中だけADBで給電中の画面常時点灯とTermux前面表示を使用し、`stay_on_while_plugged_in`は元の`0`へ復元した。最初の凍結したsoakは無効試験として上記合格値に含めていない。

## 2026-09-11 Stable基準候補実機検証（commit eabb60b / CI run 34495151505）

対象branchは`fix/macos-system-pcsc`、commit `eabb60bf7702654de20e4f347cfb6c0280ededef`、VERSION `0.1.2`、GitHub Actions run `34495151505`（17/17 jobs成功）。8 binary archive + source archive（計9 archive）および `SHA256SUMS` を生成し、チェックサム検証・展開監査済み。本候補のexact archiveを用いた各対象環境の実機検証ゲートを完了（main統合、version bump、tag、Stable Releaseはこの検証実施時点では未実施）。

| 環境 | 使用アーカイブ SHA-256 | 確認内容 |
| --- | --- | --- |
| SCS native Debian 13（x86_64 / glibc） | `fed63ce6f1e9e16ed2b56eed337c33b43bfa8906169f4fd54ed71d9b787588b5` | 2時間ソーク（300秒×24サイクル、24/24 pass）。8 receiver再取得・再チューニング・停止、内蔵カードAPDU監視120/120成功、daemon FD 29・RSS安定、終了時process/IPC残留なしを確認。receiver 192件中191件clean、receiver 7既知burst 1件（sync/queue-drop/USB error 0）。 |
| HAOS Supervisor管理Alpine add-on（x86_64 / musl） | `e77305ead8160ba48dac500813abf04c070a0b60e28dda65184f825e7ef7725e` | 2時間ソーク（24/24サイクルpass）。初回試験での地デジ過渡異常（fail扱い）を経て新規24周連続取得で完走。receiver 192件全clean（全TSエラー0）、status監視144/144、カードAPDU 144/144成功、daemon FD 31・RSS安定、終了時process/IPC残留なしを確認。 |
| Latitude 5300 / AnduinOS（x86_64 / glibc） | `fed63ce6f1e9e16ed2b56eed337c33b43bfa8906169f4fd54ed71d9b787588b5` | 30分8 receiver同時ソーク（receiver 0〜6全エラー0、receiver 7既知burstのみ）、status 30/30、カードAPDU 30/30成功。stop/reopen 15秒回帰pass。受信中USB物理切断時のexit 7有限終了、OS再起動なし再接続後の15秒復帰回帰（8/8 clean、APDU成功）、終了時process/IPC残留なしを確認。 |
| M2 Mac mini / macOS 26.6.2（arm64） | `dfde6006311c49070d340d13fba76de76343e1c0d7e7ddb981004ffb36df7655` | 30分8 receiver同時ソーク。初回終了時continuity異常（fail扱い、再現せず不採用）を経て2回目30分ソークpass（receiver 0〜6全エラー0、receiver 7既知burstのみ、APDU 30/30成功）。stop/reopen pass。受信中USB物理切断時の有限終了（exit 7）、再接続後15秒復帰回帰（全TSエラー0、APDU成功）、終了時process/IPC残留なしを確認。 |
| Pixel 9a（Android 17 / aarch64 / Bionic、Termux 0.118.3） | `378f521df009948f305c0ff90dbe34687f03e68739be554687def79096a7974a` | 正式`px4-termux`で30分8 receiver同時ソーク（receiver 0〜6全エラー0、receiver 7既知burstのみ）。status 56/56、カードAPDU 56/56成功。SIGINT/SIGHUP/SIGTERM/不正第2USB pathでの残留なし。受信中USB物理切断時のexit 7有限終了、再接続後15秒復帰回帰（8/8 clean、カード成功）、終了時process/IPC残留なしを確認。 |
| Google TV Streamer（Android 14 / API 34 / armv7a / Bionic、Termux 0.119.0-beta.3） | `01d5fd39c36928676956c7df810937c76048d487b600f65c0798d0f82c08c183` | 正式`px4-termux`で30分8 receiver同時ソーク（receiver 0〜6全エラー0、receiver 7既知burstのみ）。status 57/57、カードAPDU 57/57成功。SIGINT/SIGHUP/SIGTERM/不正第2USB pathでの残留なし。受信中USB物理切断時のexit 7有限終了、再接続後15秒復帰回帰（8/8 clean、カード成功）、終了時process/IPC残留なしを確認。 |
| IP3 GT1 / Bliss OS（Android 13 / x86_64 / Bionic、Termux 0.118.3） | `659c50d4f6461dea7109154007822b3df5d843653164d5b47da33b5381fa5564` | 正式`px4-termux`で30分8 receiver同時ソーク（receiver 0〜6全エラー0、receiver 7既知burstのみ）。status 58/58、カードAPDU 58/58成功。SIGINT/SIGHUP/SIGTERM/不正第2USB pathでの残留なし。受信中USB物理切断時のexit 7有限終了、再接続後15秒復帰回帰（8/8 clean、カード成功）、終了時process/IPC残留なしを確認。 |
| Google TV Streamer / ad-hoc APK（armv7a / Bionic） | （内部試験器具・非配布） | armv7a archiveと同SHA-256のELF payloadおよびForeground Service修正版APKを使用。Activity background状態で30分8 receiver同時ソーク（8/8 exit 0、全TSエラー0、status 31/31、APDU 31/31成功）。受信中USB物理切断時のexit 7有限終了、再接続後15秒復帰回帰（8/8 clean、APDU成功）、終了時process/IPC残留なしを確認。 |

## CIのみ

- Linux x86_64/aarch64 × glibc/muslはbuild、artifact audit、最終archive起動をCIで確認。
- この候補（commit `eabb60bf7702654de20e4f347cfb6c0280ededef`）において、Linux aarch64のバイナリは旧候補（commit `df6a1e634e5bec11961a1f0f15eedd1da22f7fee`）とbyte-identicalではなく、glibc / musl ともにこの候補での実機物理試験（チューナー・カード・IFD）は未実施です。SPEC 10.3に従い `build-tested / hardware-unverified` として扱います。

## 既知の観測事項

- 2時間の8 receiver soakではreceiver 0〜6はtransport error 0。receiver 7でTEI 10,974、continuity error 453、sync/queue-drop/USB error 0。同じ約11k TEIの署名は同一個体の参照カーネルドライバ（`tsukumijima/px4_drv`）でも同一周波数（T22）で再現しており、本ドライバ固有の回帰ではなく個体固有の既知事象として記録しています（原因および他個体での挙動は未確認）。
- FreeBSDでは接続直後に片bridgeのfirmware version queryが1回TIMEOUTする事象を2回観測。再試行後は正常。
- Windowsは本プロダクトのサポート外。
