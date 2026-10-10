# OS・環境別の検証結果

本ドキュメントは特定revisionにおける実測記録であり、将来版やすべての実行環境における動作を保証するものではありません。

## 記録方法

Stable release の検証記録は本ファイルへ日付付きで追記する。新しい records directory や template framework、汎用スクリプトは作らない。release record には候補 version/commit/CI run、10 archive（9 binary + source。binaryは8 tar archiveとWindows ZIP）と checksum/audit結果、baseline tag と各 artifact の byte-identity 判定、変更の hunk-level 影響（call-path/guard）、claim ごとの `継承` / `今回再検証` / `未認定` / `対象外`、canary/soak の選定理由（環境ID E01–E17、固定順の位置、単一OS規則による非該当を含む）、各 test の環境ID・host・device/USB ID・runtime/access path・archive SHA-256・UTC時刻・コマンド・counter・結果・ログ保存先、未実施または非該当の物理操作と理由を記録する。canonical 環境ID と手順は [`release-validation.md`](../release-validation.md) を正本とする。Android ad-hoc APK は dtv-android 所管であり本記録に含めない。

## 2026-10-11 v0.2.0 candidate 8 binary archive短時間matrix（E02–E07、E15）

candidate・CI run・archive SHA-256は下記E17節と同一（source commit `8c40d495850c332312cd4293489f400c8fb842d4`、
run `37996888969`の`release-candidate`）。外側`SHA256SUMS`で10件OKを確認した後、各archiveを試験hostへ転送し、
各runで新規展開して内側`SHA256SUMS`とmanifest（version `0.2.0`、`source_ref`はcandidate commit、platformは対象target）を確認した。
手元buildで代替していない。Q3U4は同一個体（base serial `00001205000960`、half `601/602`）、B-CAS、両RF lead、
15V adapterを使用。firmwareは2169 bytes、SHA-256 `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`。
LNBは0V（`--allow-lnb-power`なし）。日時は2026-10-10 UTC（JSTでは10-11 00:10〜01:55）。
raw log保存先はHAOSの`/config/.work/px4-0.2.0/`配下（各dirの`log/`）。ユーザー決定によりsoakとPX-M1UR/PX-S1URの追加試験は行っていない。

| target | archive SHA-256 | 環境・日時 UTC | log |
|---|---|---|---|
| linux-glibc-x86_64 | `578d67a159b35b85b8a1b30362d0f288454d90aef12dafa3a443d433ed5a2390` | E03 Latitude、AnduinOS2.0.4/kernel7.0.0-34、一般user（uid1000、`video`/`plugdev`所属）、15:10:42〜15:18:23 | `e03-09ed0d/` |
| linux-musl-x86_64 | `5219760847ba1f0ff40e54057b21b5992a8ee5ba26cf468f82763ff8692e2bbb` | E02 HAOS18.3/kernel6.18.52 Supervisor Alpine試験addon（container root）、PC/SC smoke 15:22:59〜、matrix 15:24:07〜15:28:54 | `e02-09ed0d/` |
| linux-glibc-aarch64 | `5a7a1b38a77aba48745a9ac7ff0fcc096121f668d867a45a7c83715c9fa9ac0a` | E15 Switch、Fedora42/L4T4.9.140、一般user、15:31:59〜15:43:12 | `e15g-5ce4df/` |
| linux-musl-aarch64 | `6d7f7e691c3ed4aeb2b618fc56c09fe4b23c41c5764e265bf808d3d14ac75f18` | E15同host、native Alpine3.22.5 rootful Podman container（uid0）、15:45:59〜15:51:17 | `e15m-5ce4df/` |
| darwin-arm64 | `869e598ca55f0060973aaeff736f8d477ffd0f3a801b582c960dc504dd25e1c4` | E04 M2 Mac mini/macOS26.6.2/Darwin25.6.0、16:50:14〜16:55:19 | `e04-f42ef8/` |
| android-aarch64 | `449da5c91c5995d4eac52704359e949b8bb0432136efb99efa9deaafd8a14ac6` | E05 Pixel9a/Android17/kernel6.1.162、Termux0.118.3/Termux:API0.53.0、15:56:04〜16:04:04 | `e05-88b80b/` |
| android-armv7a | `f3445e80b4408f08892eae9c4c30c5a6dd577cd2c035fead2c5870ddd736f07c` | E06 Google TV Streamer/Android14/kernel5.15.180、Termux0.119.0-beta.3、16:25:06〜16:31:01 | `e06-78174a/` |
| android-x86_64 | `75e095ecff2ae7c09a743c1d9bc37581d0ed7d30a9837112799e2248e7aec0ac` | E07 Bliss OS/Android13/kernel6.1.112、Termux0.118.3、16:39:47〜16:46:22 | `e07-95aea8/` |

コマンドと受信条件はv0.1.10節と同じ（§0.1の列挙、daemon起動、`px4ctl status/list/card-*`と
`card-apdu 90:30:00:00:00 --repeat 10`、8 receiver各30秒、USB切断clientと再接続後の新daemon、TERM/wait/runtime残留確認。
S1318000kHz slot0 / T527143kHz、receiver0〜3は`--channel BS15_0/T22`を併用）。Linux/macOSはv0.1.10の
`matrix-posix.sh`、Termuxは`termux-matrix.sh`（正式`px4-termux`の2-FD経路）をリポジトリ外で次の点だけ変更して使用した:
物理操作の検知待ちを各900秒に延長、USB抜去/再接続の判定をclaim中も消えないUSB実在数（Linux: sysfsの`0511:084a`数、
macOS: `ioreg -p IOUSB`、Termux: `termux-usb -l`、E07はsysfs照合でQ3の2 pathのみ）で行う、Termuxでは各launcher起動前に
両pathへ`termux-usb -r`を実行する、`--list-json`のkey・型・値（enclosure/devices/receiver system・LNB capability）照合を追加（Linux/macOS）。
USB/cardの物理操作は通知後にユーザーが実施した。

全8 archiveで列挙/ready8（Linux/macOSは`--list-json`照合OK、Termuxは`termux-usb -l`と`px4ctl list`）、
カード抜去で`present=no`・reader-generation 1→2・ATR/APDU exit9（`NO_CARD`）、再挿入でgeneration3・ATR/reset/APDU10成功、
各8 receiver captureの中間時点でstreaming8・APDU10成功、USB切断でclient exit7（`DISCONNECTED`）・旧daemon/launcher自己終了exit7、
再列挙後の新daemon ready・8 receiver受信・APDU10、通常停止（Linux/macOSはdaemon exit0、Termuxはrunnerの`daemon stop clean`判定）、runtime dir除去、
残留processなしを確認した。旧daemonのstop理由はE15 musl・E04で`USB_IO`、他は`DISCONNECTED`（exit7は共通）。
receiver0〜6のTS sync/TEI/CC/queue/USBは全8 archive・全3 captureで0、全receiverのbytes=packets×188。

receiver7は既知burst（[Issue #1](https://github.com/Khronos31/px4-userland/issues/1)）を記録し、burstのある区間はexit8となる。
receiver7の各matrix 30秒区間（TEI/CC、sync/queue/USBは全0。0/0の区間はexit0）:

| target | 初回 / card再挿入後 / USB再接続後 |
|---|---|
| E03 glibc x86_64 | 10980/577、11029/710、10941/605 |
| E02 musl x86_64 | 0/0、10853/567、11047/611 |
| E15 glibc aarch64 | 11075/619、10983/690、11042/593 |
| E15 musl aarch64 | 10962/576、10979/631、10980/608 |
| E04 macOS | 10981/620、11027/644、10962/558 |
| E05 aarch64 | 10918/540、11028/609、10986/611 |
| E06 armv7a | 0/0、10959/635、10952/594 |
| E07 x86_64 | 10966/582、11037/634、11020/674 |

本節の範囲ではSPEC 10.2.6aのfresh参照比較（`px4_drv`、§0.3 X-R7REF）を実施していない。SPEC §10.5の0.2.0に限る受入判断の対象はE17である。

E02は試験addonのDockerfile・candidates・config.yaml・Supervisor optionsを`/config/.work/px4-0.2.0/e02-backup/`へ退避し、
candidate musl archiveを`candidates/`へstage、bashとoverlay entrypointを追加した一時Dockerfileでrebuildして実行した。
entrypointはarchive/firmware SHA-256照合後、既存`userland-stable-run`のPC/SC smoke（q3u4、serial `00001205000960`、30秒、siano0、LNB0）を実行し
（status passed、daemon/pcsc/card exit0、PC/SC reader確認、receiver0〜6 exit0・receiver7 exit8）、続けてmatrixを実行した。終了後addonはstopped。
ユーザー指示によりaddonの元設定への復元は行っていない（退避は保持）。
E15 muslはv0.1.10試験image `localhost/px4-010-matrix:alpine322`へ`apk add jq`だけを加えたimage
`localhost/px4-020-matrix:alpine322`（ID `1d728e15af20`）の`--rm`一時containerで実行し、終了後container消滅を確認した。
E05/E06/E07のUSB permissionは各launcher起動前の`termux-usb -r`で取得した。E06の再接続後（新path `/001/105`、`/001/106`）は
第1 pathの要求がTermux:API側で`Permission request timeout`を返した後、両pathでlauncherがready8となった。
E07はOS起動USB等を接続したまま、sysfs vendor/product/half serial照合でQ3の2 pathだけを選択した。

試行の逸脱:

- E05の初回試行（`e05-88b80b/attempt1-invalid-longpath/`）は試験用runtime dirを長い作業dir配下に置いたrunで、px4dが
  `serial endpoint: INVALID_ARGUMENT`を出してready前に終了した（GATE-INCOMPLETE、物理操作前）。runner側の設定不備として無効とし、
  短いruntime dir（`$PREFIX/tmp/e05r2`、endpoint path 102 bytes）で新規展開して再実施した上表のrunを記録する。
- E05の2回のTermux preflightで`termux-info`を実行し、Androidのclipboardを上書きした。E06/E07ではこれを除いた。

## 2026-10-10 v0.2.0 candidate E17 Windows 11 x64 実機試験（Q3U4必須matrix PASS / soak注記付き受入）

- candidate: version `0.2.0`、source commit `8c40d495850c332312cd4293489f400c8fb842d4`（`feat/windows-phase1`、
  PR [#51](https://github.com/Khronos31/px4-userland/pull/51) のmerge commit）。tag・main merge・release公開はしていない。
  本節はE17（`windows-x86_64`行）だけの記録であり、他8 binary archiveの短時間matrix（E02–E07、E15）は未実施。
- 結果概要: Q3U4は必須matrix・全局確認・残り4項目がPASS、2時間soakは注記付き受入。PX-M1UR/PX-S1URは同日に
  Windows profile試験（30分soak、card/USB抜差し）を実施しPASS（same-lease retuneは未確認）。ready行の非ASCII切断は
  既知の制限（[Issue #52](https://github.com/Khronos31/px4-userland/issues/52)）。
- CI: push run [`37996888969`](https://github.com/Khronos31/px4-userland/actions/runs/37996888969) と
  dispatch run [`37996927928`](https://github.com/Khronos31/px4-userland/actions/runs/37996927928) は両方20 jobすべてSUCCESS
  （Windows cross-build/PE audit/packaging、Windows Server 2022 offline testとrelease archive CLI smokeを含む）。
  両runの`release-candidate`を別の空ディレクトリへ取得し、各`sha256sum -c SHA256SUMS`が10件OK、10 archiveを個別`cmp`で
  byte-identical、外側`SHA256SUMS`もbyte-identical（SHA-256
  `38c816b9dcd1dc08dbcc047861ff797fcf6fb8a4bcbdff6b1d0c2e68ad427dd8`）。9 binaryのmanifestはversion `0.2.0`、
  `source_ref`はcandidate commit。両runのrunner image・実効toolchain inventoryの突合は本節では行っておらず、
  SPEC 10.5-2のinput一致判定は未完了（pending）。
- SPEC 10.5-2 input突合（2026-10-11、両runのjob log）: 20 jobのrunner imageは両runで同一（Ubuntu x86_64
  `20261004.327.1`、Ubuntu arm64 `20261004.142.1`、macOS arm64 `20260831.0302.1`、Windows Server 2022
  `20261004.326.1`）。artifact生成toolchainの観測値も両runで一致: musl GCC14.2.0-r6 / binutils2.44-r3 / musl-dev1.2.5-r12
  （Linux x86_64/aarch64 static jobの48件のapk package versionが両runで同一）、glibc IFD GCC10.2.1（Debian11 image digest
  `6f519a81440354a85eb592c5f32109ab80605f6b892455983a6f618bf87fabe9`）、Alpine image digest
  `5291449c3df73caf6ed85e649dec1b9e818b39a5d8c871e97afc13e9cd5e8fa8`、AppleClang15.0.0.15000309 / Xcode15.4（`15F31d`）/
  macOS SDK14.5 / ld-1053.12、Android NDK r27d / Clang18.0.4 / API24、Windows llvm-mingw `20250910`（UCRT）/ Clang21.1.1。
  offline Ubuntu testsはGCC13.3.0。libusb1.0.30 sourceはpinned checksum
  `fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf`で照合OK（両run）。時刻・所要時間・一時path・run/artifact ID・
  build並列順・artifact zip digest・region/worker IDを除いた両runのjob logに、上記以外の差分はなかった
  （Docker layer検証行の出現jobのみ異なる）。runner imageと実効build inputが一致し、SPEC 10.5-2の一致と判定した。
- archive（両run共通）:

| archive | SHA-256 | 本節での実機status |
|---|---|---|
| `px4-userland-0.2.0-windows-x86_64.zip`（882833 bytes） | `c409edea022bc0c5613bf168248f55efbd408603f044f4bc1980b0605122bdc3` | E17で今回再検証（下記） |
| `px4-userland-0.2.0-linux-glibc-x86_64.tar.gz` | `578d67a159b35b85b8a1b30362d0f288454d90aef12dafa3a443d433ed5a2390` | 未実施 |
| `px4-userland-0.2.0-linux-musl-x86_64.tar.gz` | `5219760847ba1f0ff40e54057b21b5992a8ee5ba26cf468f82763ff8692e2bbb` | 未実施 |
| `px4-userland-0.2.0-linux-glibc-aarch64.tar.gz` | `5a7a1b38a77aba48745a9ac7ff0fcc096121f668d867a45a7c83715c9fa9ac0a` | 未実施 |
| `px4-userland-0.2.0-linux-musl-aarch64.tar.gz` | `6d7f7e691c3ed4aeb2b618fc56c09fe4b23c41c5764e265bf808d3d14ac75f18` | 未実施 |
| `px4-userland-0.2.0-darwin-arm64.tar.gz` | `869e598ca55f0060973aaeff736f8d477ffd0f3a801b582c960dc504dd25e1c4` | 未実施 |
| `px4-userland-0.2.0-android-aarch64.tar.gz` | `449da5c91c5995d4eac52704359e949b8bb0432136efb99efa9deaafd8a14ac6` | 未実施 |
| `px4-userland-0.2.0-android-armv7a.tar.gz` | `f3445e80b4408f08892eae9c4c30c5a6dd577cd2c035fead2c5870ddd736f07c` | 未実施 |
| `px4-userland-0.2.0-android-x86_64.tar.gz` | `75e095ecff2ae7c09a743c1d9bc37581d0ed7d30a9837112799e2248e7aec0ac` | 未実施 |
| `px4-userland-0.2.0-source.tar.gz` | `b4c699ad85e73655bf9931c4260bbd64098d3509b7a418311ab33666d4832865` | corresponding source |

- RAW_IO修正の経緯: 先行candidate（`9e2fa3e`、Windows ZIP SHA-256
  `fdeb53924de621bdce0918621a8ac7c584519a7ac7949fb8d3123fd32e7ba575`）のE17 8受信同時captureで、bridge単位の
  一斉CC欠落を観測した（[Issue #50](https://github.com/Khronos31/px4-userland/issues/50)）。`72155a4`はWindows
  backendのTS endpoint 0x84でWinUSB RAW_IOを有効化し、packet-aligned転送（153600 bytes）にする。PR #51の
  交互比較（各8受信30秒）では`9e2fa3e`が24回中11回で欠落、PR #51 buildは12回中0回（記録は#51 comment、
  HOME-PC `C:\px4-e17\runs\cmp-rawio1\`）。先行candidateでのE17試行は採用せず、本節はすべて`8c40d49`の
  exact ZIPで再実施した。`8180783`以後の変更にはPOSIX buildへ入るhunk（`libusb_transport.cpp`、
  `libusb_transport_internal.h`、`posix_ipc.h`）を含むため、他artifactの影響判定は8環境回帰で別途記録する。

### 環境・共通条件

- 環境: E17、host `HOME-PC`、Windows 11 Pro x64 build 26300.9457（DisplayVersion 26H2。registryの
  ProductNameは`Windows 10 Pro`を返す）。`chcp`既定code page 932、ACP/OEMCP 932（`e17rest-f83190`で記録）。
  `e17rest-f83190`はWindows PowerShell 5.1.26100.9444（Desktop）で実行した。それ以前のrunのPowerShell versionは
  記録していない（同hostの既定shellはPowerShell 7.6.6）。
- 機器: PX-Q3U4（base serial `00001205000960`、USB `0511:084a` half `…9601`/`…9602`、bus 5 port `5-1.2.1`/`5-1.2.2`）、
  PX-M1UR（`0511:0854`、serial `000000000000001`）、PX-S1UR（`0511:0855`、serial `000000000000001`）。
  いずれもユーザーが事前にWinUSBへbindingし、`Get-PnpDevice`で`Status OK`/service `WinUSB`を確認した
  （agentはdriver/INFを変更していない）。B-CASのATRは3機種とも`3b:f0:12:00:ff:91:81:b1:7c:45:1f:03:99`。
- firmware: 利用者提供 `C:\px4-e17\fw\it930x-firmware.bin`（2169 bytes、SHA-256
  `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`）。
- archive検証: `e17r2-fef601`と`e17soak-e2c170`はそれぞれ新しい展開先へ展開し、ZIP SHA-256一致、内側`SHA256SUMS`
  16件bad 0、ZIP entry 17件の照合mismatch 0、`manifest.json`のversion/source_refを確認した。以後のrun（multi、M1UR、
  S1UR、電圧、全局確認）はrunごとの再展開をせず、soakの展開先`e17soak-e2c170\extract`を再使用した
  （multi開始時に内側`SHA256SUMS`を再照合しbad 0）。`e17rest-f83190`は非ASCII path（下記）へ新規展開して同じ照合を行った。
  `px4d.exe` SHA-256
  `006de9210ee910fc230342b5429213bf76bd81b4aed6e0f44a6cbb0163d244eb`、`px4-ts.exe`
  `5f400a777d3dd19ec9fc27fdfca5327b3cdf25a95d2a437a9d2eed551c5ffa97`、`px4ctl.exe`
  `d4f20434c6ba139257277f9bb812438914d91fd92d3292e26b489ffc19351c07`。
- runtime: タスク専用の親`C:\px4e17\L`（owner=現在user SID `S-1-5-21-…-1004`、protected DACL、ACEは現在userの
  FullControlだけ）をowned process（px4d/px4-ts）だけへprocess-scopedの`LOCALAPPDATA`として渡し、その下に
  run別の`--runtime-dir`を置いた。profile ACL・global env・driverは変更していない。daemonは毎回
  `--exit-on-stdin-eof`で起動し、停止はstdin closeによるcooperative shutdownだけを使った（force killなし）。
- 受信条件（Q3U4 8受信）: receiver 0/1/4/5 = ISDB-S `--frequency-khz 1318000 --slot 0`（BS15/TS0）、
  2/3/6/7 = ISDB-T `--frequency-khz 527143`、`--output NUL`。CARDは`px4ctl card-status/card-atr/card-reset/
  card-apdu 90:30:00:00:00 --repeat 10`。
- raw log: HOME-PC `C:\px4-e17\runs\<run>\`（HAOSへは未転送）。時刻はUTC、括弧内はJST。

### 必須短時間matrix（`e17r2-fef601`、2026-10-09 22:13:14〜22:19:24 UTC／10-10 07:13〜07:19 JST）

- preflight: stray processなし、Q3U4 2 half WinUSB OK、`px4d --list`/`--list-json` rc 0。Q3U4はready、8 receiver、
  `serial_unique=true`、ISDB-S receiver（0/1/4/5）は`lnb_15v_supported=true`、ISDB-T receiver（2/3/6/7）はfalse。
  JSONのcapability値はboolean。
- RAW_IO確認（正式runとは別daemon、`LIBUSB_DEBUG=4`、r2/r6 ISDB-T 5秒）: 両bridgeの別threadで
  `enabled RAW_IO for endpoint 84`、停止時に`disabled`各2件、153600 bytes readが198回・他サイズ0、daemon exit 0、残留なし。
- 手順と結果（release-validation §3の1–7。各物理操作はユーザーが実施、通知から5分以内）:

| 段階 | 結果 |
|---|---|
| daemon1 ready後CARD | card-status/atr/reset rc 0、APDU 10/10 rc 0（応答61 bytes、SW `90 00`） |
| gen1 8受信同時30秒 | 8 stream、capture中APDU rc 0。receiver 0–6はrc 0、sync/TEI/CC/queue/USB 0 |
| B-CAS抜去 | `present=no initialized=no reader-generation=2`、card-atr/card-apduとも`NO_CARD`（exit 9） |
| B-CAS再挿入 | generation 3、ATR/reset/APDU 10回 rc 0 |
| gen2 8受信同時30秒 | receiver 0–6 全counter 0 |
| USB切断（1 receiver client稼働中） | client `DISCONNECTED` exit 7（packets 1111365）、daemon `px4d stopped: USB_IO` exit 7、runtime残留なし、stray 0 |
| USB再接続 | `--list`で同一serial・8 receiver readyを再列挙。旧daemonは終了済みのため、新daemon2を同じcandidateから起動 |
| daemon2 ready後CARD・gen3 8受信同時30秒 | ATR/reset/APDU rc 0、receiver 0–6 全counter 0 |
| cooperative stop | daemon2 exit 0、runtime残留なし、stray px4d/px4-ts 0 |

receiver別packet数（30秒、bytes=packets×188）: S受信は約47.7万〜48.0万、T受信は約34.5万〜34.6万。
receiver 7は3世代ともexit 8（`PROTOCOL_ERROR`）で、sync/queue/USB 0:

| 世代 | receiver 7 TEI / CC / packets |
|---|---|
| gen1（初回） | 10953 / 638 / 341866 |
| gen2（card再挿入後） | 10965 / 576 / 341768 |
| gen3（USB再接続後） | 10985 / 558 / 342241 |

判定: release-validation §3のE17必須項目はPASS。receiver 7は下記「receiver 7」の扱いによる。

### E17手順の残り4項目（`e17rest-f83190`、2026-10-10 10:46:22〜10:47:04 UTC／19:46〜19:47 JST）

Q3U4のみ接続、アンテナ接続、daemonはopt-inなし、物理操作なし。release-validation E17節のrunnable setup
（タスク専用の保護された親を`TEMP`配下に作りprocess-scopedの`LOCALAPPDATA`として渡す）に従った。

- 環境記録: Windows PowerShell 5.1.26100.9444（Desktop、`powershell.exe`）、`OSVersion` 10.0.26300.0、`chcp` 932、
  ACP 932、OEMCP 932、`[Console]::OutputEncoding` 932。既定`LOCALAPPDATA`（`C:\Users\yunomin61\AppData\Local`）は
  owner=現在user、protected=False、継承ACEがSYSTEM/Administrators/現在userの各FullControl（read-only記録）。
- 非ASCII path: タスクroot `C:\px4-e17-試験é-f83190`（日本語とCP932にない`é`を含む）へ検証済みZIPを新規展開
  （ZIP SHA-256一致、内側16件bad 0、manifest version `0.2.0`/source_ref `8c40d49…`）。firmwareを
  `…\ファームé.bin`へcopyしSHA-256一致。親`C:\Users\YUNOMI~1\AppData\Local\Temp\px4e17lap-98e6e00c`、
  `--runtime-dir`は`<親>\試験é`。AF_UNIX sample長はworker 88、control 105、stream 104 bytes（<108）。
  daemonはready（`ready=yes`）、firmware load成功。8受信同時30秒でreceiver 0–6はrc 0・全counter 0
  （packets 477344〜478774 / 345411〜345847）、receiver 7はexit 8（TEI 11047、CC 674、sync/queue/USB 0）。
  receiver 2は`--output …\出力é-r2.ts`へ書き込み、file size 64937268 bytes = 報告bytes（345411×188）、先頭byte 0x47。
  card-status/ATR/reset rc 0、APDU 10回 rc 0（SW `90 00`）。argv・file open（firmware、runtime/endpoint、output）は
  ACP 932下で壊れなかった。
- endpoint ACL（daemon稼働中）: `<RT>`、`px4-userland`、`00001205000960`、`control.sock`、`stream.sock`の5件すべて
  owner=現在user SID、protected=True、ACEは現在user SIDのFullControl 1件だけ（継承なし）。PASS。
- busy: daemon1稼働中に同じ`--device`で2つ目のpx4dを起動。同じ`--runtime-dir`、別の`--runtime-dir`
  （`<親>\b2`）とも`device open: BUSY`、exit 4。後者のruntime dirにentryは作られず、daemon1はready=yesのまま
  受信を継続した。PASS。
- 停止: daemon1 exit 0、`<RT>`・`b2`・親とも空になり削除、stray px4d/px4-ts/px4ctl 0。
- **既知の制限（[Issue #52](https://github.com/Khronos31/px4-userland/issues/52)、0.2.xで修正予定）**: 非ASCIIの`--runtime-dir`ではpx4dのstderrが最初の非ASCII文字で途切れた。raw stderr
  121 bytesは`px4d ready: device=00001205000960 endpoint=C:\Users\YUNOMI~1\AppData\Local\Temp\px4e17lap-98e6e00c\`
  （0x5c）で終わり、改行もない。endpoint自体は正しく作成・利用できた。追加切り分け（同日、Q3U4のみ、PS5.1）で、
  原因はready行だけが`fprintf(stderr, "…%ls\n")`でwide pathを出力し、CRTが既定"C" localeのためU+00FFを超える文字で
  変換失敗（EILSEQ、戻り値-1）することと判明した。cmdの`2> file`、`Start-Process -RedirectStandardError`、
  `chcp 65001`併用のいずれも同じ77 bytesで途切れ、ASCIIのみの既定`LOCALAPPDATA`では行末CRLFまで140 bytes出力された。
  stream error flagは立たず後続の`fprintf`は出力される（同CRTの最小programで確認）が、改行が欠けるため次の行が
  同一行に連結される。U+0080〜U+00FFはLatin-1の1 byteで出て文字化けする。px4ctl/px4-tsはpathを出力しない。
  既定`LOCALAPPDATA`はlong pathのため、日本語user名では既定設定でも再現し得る。daemonの動作への影響はない。
- 判定: 4項目のうちcode page/PowerShell記録、endpoint ACL、busyはPASS。非ASCII pathはargv・file open・endpoint作成の
  観点でPASS。ready行のstderr切断は2026-10-10ユーザー決定により0.2.0の既知の制限（[Issue #52](https://github.com/Khronos31/px4-userland/issues/52)）として扱い、
  非ASCII pathは「既知の制限付きPASS」とする。
- 失敗・診断試行（保存済み）: `e17rest-defff5`はPowerShell 7.6.6で実行し、手順の
  `[System.IO.Directory]::CreateDirectory($LAP, $acl)`がPowerShell 7（.NET）に存在しないoverloadで停止（daemon未起動）。
  `e17rest-80d027`（Windows PowerShell 5.1）は手順どおり`$RT`を事前作成しなかったため、px4dが
  `serial endpoint: NOT_FOUND`（exit 3）で終了した（あわせてscriptの初回status pollがPS5.1でnative stderrを
  終了errorに変換した）。同じ展開物での切り分け（`e17rest-80d027\diag`）では、ASCIIの未作成runtime dirは
  同じNOT_FOUND/exit 3、owner=現在user・protected DACLで事前作成したASCII/非ASCII runtime dirはともにready・exit 0・
  残留なし。正式runではこの事前作成を加えた。

### 3機種同時接続・同一serial（`e17multi-c80689`、2026-10-10 約09:01 UTC／18:01 JST）

- Q3U4・M1UR・S1URを同時接続（M1UR/S1URはUSB hub経由、port `5-1.4.1`/`5-1.4.2`）。`--list`/`--list-json` rc 0。
  M1UR（`ISDB-T/S`）とS1UR（`ISDB-T`）が同じserial `000000000000001`の別enclosureとして現れ、双方
  `serial_unique=false`、`lnb_15v_supported=false`。Q3U4はS receiverだけtrue、T receiverはfalse。全値boolean。
- `px4d --device 000000000000001 --firmware <FW> --runtime-dir <RT>`は`device open: INVALID_ARGUMENT`、両候補を
  列挙して`--usb-path`と`--instance`を要求し、exit 2。指定runtime dirにentryは作成されず、stray processなし。
  USB interface claimの有無は直接観測していない。
- 同時接続中は手順どおり受信・カード・LNBの試験を行っていない。

### PX-M1UR単独（`e17m1ur-5be907`、2026-10-10 09:03〜09:19 UTC／18:03〜18:19 JST）

| run | daemon | 結果 |
|---|---|---|
| A（09:03:55〜09:04:40） | opt-inなし | `--list`/`--list-json` OK。ISDB-S 0V 30秒 packets 478964 全counter 0。CARD ATR/reset/APDU 10 rc 0。15V要求は`UNSUPPORTED` exit 3、packet 0。daemon exit 0、残留なし。ISDB-T 527143は約6秒で`TIMEOUT`（packet 0、exit 8） |
| B1（09:04:40〜09:05:21） | `--allow-lnb-power` | **無効試行**: daemonが40回のstatus pollでreadyにならず（`connect: NOT_FOUND`）、後続の15V要求のexit 3も`NOT_FOUND`によるもので拒否の証拠にしない。px4dのstdout/stderr/exit値は保存されず、停止確認のログ行も同秒に「35秒以内に終了せず」と記録され整合しない。stray processなし。原因未解明の単発事象として保持 |
| b2（09:07:55〜09:08:15） | `--allow-lnb-power` | daemon 1秒でready。15V要求は`UNSUPPORTED` exit 3、packet 0。直後status ready。ISDB-S 0V 5秒 packets 81576 全counter 0。ISDB-T 527143/557142は約6秒で`TIMEOUT`。daemon exit 0、残留なし |
| c3（09:12:55〜09:13:28） | opt-inなし | 同じ配線で再試行。ISDB-T 527143（`--tune-timeout-ms 30000`）とT27/T25/T22がすべて約6秒で`TIMEOUT`（packet 0）。対照のISDB-S 0V 5秒は全counter 0 |
| d4（09:17:56〜09:19:10） | opt-inなし | ユーザーが分配器を1段減らした後。ISDB-T 527143 30秒 packets 345946、T27 30秒 packets 345951、ISDB-S 0V 5秒 packets 82389、いずれも全counter 0、rc 0。daemon exit 0、残留なし |

判定: M1URの15V要求拒否はopt-inなし（A）・あり（b2）の双方で`UNSUPPORTED` exit 3（PASS）。GPIO無書込みは
offline/mock試験に依拠し、実機で個別測定していない。A/b2/c3のISDB-T失敗は過剰分配による受信レベル不足と判断し
（d4で解消、同機はLinuxでISDB-T受信済み）、失敗試行として保持する。

### PX-S1UR単独（`e17s1ur-653101`、2026-10-10 09:23:16〜09:24:27 UTC／18:23〜18:24 JST）

- `--list`/`--list-json` OK（`ISDB-T`、`lnb_15v_supported=false`）。ISDB-T 527143 30秒 packets 345946、`--channel T27`
  30秒 packets 345952、いずれも全counter 0、rc 0。CARD status/ATR/reset/APDU 10 rc 0。
- ISDB-S要求（1318000 slot 0）は受信開始前に`INVALID_ARGUMENT` exit 2、packet 0。daemon exit 0、残留なし。PASS。

### PX-M1UR / PX-S1UR Windows profile試験（`m1urq-92a667`、`s1urq-81b3aa`、2026-10-10 11:37〜13:18 UTC／20:37〜22:18 JST）

各機種を単独接続し（USB port `5-1.4`／`5-1.2`、PnP status OK、service WinUSB）、qualification runごとに検証済みZIP
（SHA-256 `c409edea…bdc3`一致）を新規展開して内側`SHA256SUMS` 16件bad 0、manifest version `0.2.0`/source_ref
`8c40d49…`、firmware SHA-256 `5213a5a3…b484`一致を確認した。物理操作のsub-run（`phys-*`、`usb-*`）は親runの展開物を
使い、開始時に`SHA256SUMS`を再照合した（16件bad 0）。runtime dirは保護された親（owner=現在user、protected、ACE OK）
配下のASCII path、daemonはopt-inなし。各daemonは1秒でready、全daemonのstopは残留なし・stray 0。
`--list`/`--list-json` rc 0、M1URは`ISDB-T/S`、S1URは`ISDB-T`、いずれも`serial_unique=true`、`lnb_15v_supported=false`。

**PX-M1UR（`m1urq-92a667`、11:37:40〜12:09:23 UTC／20:37〜21:09 JST、`DONE errorcount=0 verdict=PASS`）**

| 区分 | 結果 |
|---|---|
| 受信（daemon d1） | ISDB-T 527143 10秒 115834 packets、ISDB-S 1318000 slot0 0V 10秒 160724、ISDB-S 1049480 slot0 0V 10秒 116667。いずれもrc 0、sync/TEI/CC/queue/USB 0 |
| stop/reopen（d1） | T→T→S→Tの5秒×4（59531／59532／81574／58714 packets）、全counter 0、rc 0。前後のstatus ready/free、usb/protocol errors 0 |
| card（d1、d2） | card-status/ATR/reset/APDU 10回が各daemonの前後でrc 0（SW `90 00`、`reader-generation=1`） |
| daemon再起動（d1 exit 0 → d2） | T 5秒 58716、S 0V 5秒 82389 packets、全counter 0 |
| 30分soak（d2） | ISDB-T 527143 1800秒（wall 1803秒）、**20,668,426 packets / 3,885,664,088 bytes**、sync/TEI/CC/queue/USB 0、empty intervals 128,166、exit 0。受信中に約30秒ごと`px4ctl card-apdu --repeat 5`を60回（**APDU 300/300**、rc 0）。5分ごとstatus 6回すべてready/streaming・errors 0 |
| 資源（px4d） | handle 189→197→195（snap2〜6一定）→189、private bytes 18.5→22.1→22.0 MB→18.4 MB、thread 10→7→6。disposition `安定` |
| card抜去/再挿入（`phys-6af665`、12:11:28〜12:14:10 UTC） | 抜去でgeneration 1→7、`card-present=no`、`reader-generation=2`、ATR/reset/APDUは`NO_CARD` exit 9。cardなしでT 5秒（58715）全counter 0。再挿入でgeneration 13、`reader-generation=3`、status/ATR/reset/APDU 10 rc 0、T 5秒（58716）全counter 0。**VALID** |
| USB切断（`phys-6af665`のUSB step） | **INVALID（試験orchestratorの誤検出、製品不具合ではない）**: 抜去検出に`px4d --list`を使ったため、d3がclaim中のdeviceを`status=open_failed`と読み、未抜去のまま12:14:14 UTCに「抜去観測」とした。12:15:31 UTCの時点でd3はready・streaming、clientも受信継続。orchestratorを停止し、d3はstdin EOFで終了（exit値未取得）、残留なし（`INVALID-usb-step.txt`） |
| USB切断・再接続（`usb-714d78`、12:18:13〜12:20:35 UTC、PnP不在2回連続で検出） | 受信中に抜去: in-flight `px4-ts`は`DISCONNECTED` exit 7（739258 packets書込み）、旧daemonは`shutdown cleanup: USB_IO`／`px4d stopped: USB_IO`で自ら exit 7、runtime残留なし・stray 0。再挿入後（port `5-1.2`、新address）`--list` ready、新daemon d4が1秒でready、card status/ATR/reset/APDU 10 rc 0、T 10秒（116651）・S 0V 10秒（160725）全counter 0、d4 exit 0。**PASS**（restart-based recovery） |
| 15V拒否 | 先行`e17m1ur-5be907`（A、b2）でopt-inなし・ありとも`UNSUPPORTED` exit 3（上記） |

**PX-S1UR（`s1urq-81b3aa`、12:22:21〜12:53:56 UTC／21:22〜21:53 JST、`DONE errorcount=0 verdict=PASS`）**

| 区分 | 結果 |
|---|---|
| 受信（d1） | ISDB-T 527143 10秒 115835、557142 10秒 115839 packets、全counter 0、rc 0 |
| ISDB-S拒否 | 1318000 slot0 0V要求は受信開始前に`INVALID_ARGUMENT` exit 2、packets 0。直後status ready/free |
| stop/reopen（d1） | 527143→527143→557142→527143の5秒×4（58714／59532／59536／59530）、全counter 0 |
| card（d1、d2） | 前後でstatus/ATR/reset/APDU 10 rc 0 |
| daemon再起動（d2） | 527143・557142各5秒（58715／58720）、全counter 0 |
| 30分soak（d2） | ISDB-T 527143 1800秒（wall 1803秒）、**20,667,610 packets / 3,885,510,680 bytes**（Linux S1UR記録と同数）、全counter 0、empty intervals 128,048、exit 0。受信中**APDU 300/300**（60回×5）。status 6回すべてready/streaming・errors 0 |
| 資源（px4d） | handle 189→197→195→189、private bytes 18.5→22.1→22.0 MB→18.4 MB、thread 10→7→6。disposition `安定` |
| card抜去/再挿入・USB切断/再接続（`phys-df3e4e`、13:07:25〜13:17:48 UTC、`DONE errorcount=0 verdict=PASS`） | 抜去でgeneration 1→7、`NO_CARD` exit 9、cardなしT 5秒全counter 0。再挿入でgeneration 13、card操作rc 0、T 5秒（59532）全counter 0。受信中のUSB抜去でclient `DISCONNECTED` exit 7（423466 packets）、旧daemon `USB_IO`で exit 7、残留なし。再挿入後の新daemon d4は1秒でready、card操作rc 0、527143・557142各10秒（115834／115839）全counter 0、d4 exit 0 |

- 失敗・無効試行（保存済み）: `m1urq-cce233`（11:33:51 UTC、`Get-FileHash`が見つからずscript停止）、
  `m1urq-65a196`（11:35:23 UTC、ZIP照合後に`Expand-Archive`の`DestinationPath`がnull）はいずれもdaemon起動前の
  script不具合で、device操作・残留なし。上記`phys-6af665`のUSB stepは無効（card stepは有効）。
- 手順の置換・未確認:
  - SPEC 10.2.7/Linux認定のPC/SC併走APDUは、Windows Phase 1にPC/SC adapterがないため受信中の直接`px4ctl card-apdu`
    （各300/300）で置き換えた。native-card-adapterは対象外（N/A）のまま。
  - 同一lease retuneはWindowsで未確認（`retune_tool`はPOSIX IPC専用、`px4-ts`にretune機能なし）。stop/reopenと
    別leaseでのT/S切替だけを確認した。
  - 同一daemonのUSB自動再接続は対象外（旧daemonは`USB_IO`で終了し、新規起動で復旧）。同一serial衝突中の
    `--usb-path`による個別起動は未確認。
- 判定: 両機種とも10.2.7の共通認定項目のうち、Windows 11 x64 native libusb pathで識別、firmware、機種固有system
  （M1UR T/S 0V・15V拒否、S1UR T・ISDB-S拒否）、capture、stop/reopen、daemon再起動、status、card抜去/再挿入・
  ATR/reset/APDU、USB切断後の復旧、cleanup、30分連続受信をPASSとした。tunerとcard coreを`今回再検証`、
  same-lease retuneを`未認定`とする。

### Q3U4 LNB端子間電圧（`e17volt-183347`、`e17volt0-183600`、2026-10-10 09:33〜09:36 UTC／18:33〜18:36 JST）

- ユーザーの明示許可を得て、Q3U4単独、衛星F端子からアンテナ線を外した無負荷開放端で、ユーザーがテスター
  （DC 20V range）を芯線・外導体間に当てて目視した。daemonは`--allow-lnb-power`、receiver 0、ISDB-S 1318000 slot 0、
  `--tune-timeout-ms 30000 --duration-seconds 120`。
- 15V要求（09:33:48〜09:34:18 UTC）: アンテナなしでlockできず30秒で`TIMEOUT`（exit 5、packet 0）。ユーザー観測は
  「tune中の30秒間15Vを維持し、終了後すぐ0V」。意図した120秒保持ではなく、tune timeoutで終了した点を注記する。
- 0V要求: 同runの0V phase（09:34:48〜09:35:19）はテスター外れのため不採用。`e17volt0-183600`で再実施
  （09:36:02〜09:36:32、同じくexit 5）し、ユーザー観測は「ずっと0V」。
- 両runともdaemon exit 0、最終status全receiver free、runtime残留なし、stray 0。数値の小数点以下は記録していない。
  要求はreceiver 0（bridge 1）からだけで、bridge 2側S receiverからの要求と代表負荷時の給電能力は未測定
  （`loaded supply unverified`）。

### 全tuner × 局の受信確認

- `e17chan-0ecb7a`（2026-10-10 09:53:07〜10:01:57 UTC／18:53〜19:01 JST）: daemonはopt-inなし。各captureは
  `--channel <ch> --tune-timeout-ms 10000 --duration-seconds 10`、各roundでS 4 receiver・T 4 receiverを同時実行し、
  全receiverが各局を1回ずつ受ける並び。事前sanity（T527143、S1318000 slot0）OK。終了後CARD rc 0、APDU 10 OK、
  daemon exit 0、残留なし。
  - 地上波13 ch（T16/17/19/21–27/30/31/32）× receiver 2/3/6/7: 東京の8局（T16/21/22/23/24/25/26/27）は32/32で
    全counter 0。T31/T32は4/4。T30はreceiver 7だけexit 8（TEI 11088、CC 584、既知burst形）、他3は0。T17は
    receiver 7でTEI 1（CC 0）、他3は0。T19は4 receiverとも`TIMEOUT`（packet 0）。
  - 衛星38 slot（BS 26 slot、CS 12 transponder CS2–CS24）× receiver 0/1/4/5: BS 26 slotは104/104で全counter 0。
    CS2–CS18とCS22は全receiverで0。CS20はreceiver 5だけTEI 4626/CC 270（他3は0）、CS24は4 receiverすべてTEI
    920〜3070/CC 14〜323。sync/queue/USBは全capture 0。
- 不採用の先行試行: `e17chan-f165a7`、`e17chan-db2a63`（09:44〜09:45 UTC。S指定の引数組立て誤りでexit 2、
  T受信もTIMEOUT）、`e17chan-366e07`（09:46〜、アンテナ線未接続のため全TIMEOUT。ユーザーが停止を要求）。
  いずれもcapture未成立で、daemon停止・残留なしを確認。
- `e17rerun-f326ae`（10:22:13〜10:23:07 UTC／19:22〜19:23 JST、ユーザーがアンテナ側の新しい分配器を交換した後）:
  T19はreceiver 2/3/6でrc 0・全counter 0、receiver 7はTEI 16069/CC 829（exit 8、既知burst形だが件数は他の観測より多い）。
  CS20は4/4で0。CS24はreceiver 0/1/4で0、receiver 5だけTEI 26/CC 2。対照のT27（4/4）とBS15_0（4/4）は全counter 0。
  終了後CARD/停止/残留確認OK。
- 判定: T19/CS20/CS24の初回失敗は受信設備（新しい分配器）由来と判断した（同時期にユーザーがWebTS.appでも
  T19を受信できないことを確認、交換後に回復）。初回結果は失敗として保持する。CS24 receiver 5の残りと
  T17 receiver 7のTEI 1は、CS最上位帯の弱電界・単発として扱う。ユーザーは2026-10-10に受信確認を完了と判断した。

### 2時間soak（`e17soak-e2c170`、2026-10-09 22:27:59〜10-10 00:28:16 UTC／10-10 07:28〜09:28 JST）

- ユーザー決定: 対象E17、Q3U4、2時間**連続**（30分×4分割ではない）。直前に60秒のsmoke（`e17soak-smoke583`、PASS）。
- 内容: daemon（opt-inなし）上で8受信を同時に7200秒（上記の周波数）。10分ごと12回、`px4ctl status`、
  `card-apdu --repeat 10`、px4d/px4-tsのhandle数・working set・private bytes・thread数を記録。retune/stop-reopenは
  soak中に行っていない。開始前・終了後のCARD status/ATR/reset/APDU 10はrc 0。
- snapshot 12回すべてready、card present、streaming 8、APDU rc 0（10/10）、daemon status `usb-errors=0 protocol-errors=0`。
- 終了時counter（全receiver exit 8。sync/TEI/queue/USBはreceiver 0–6で0）:

| receiver | packets | continuity errors | TEI |
|---|---|---|---|
| 0（S、bridge 1） | 114473712 | 8 | 0 |
| 1（S、bridge 1） | 114473330 | 8 | 0 |
| 2（T、bridge 1） | 82666557 | 12 | 0 |
| 3（T、bridge 1） | 82667332 | 12 | 0 |
| 4（S、bridge 2） | 114474034 | 5 | 0 |
| 5（S、bridge 2） | 114472758 | 5 | 0 |
| 6（T、bridge 2） | 82667031 | 11 | 0 |
| 7（T、bridge 2） | 82662682 | 629 | 10975 |

- 資源推移（px4d、12点）: handle 243→242（min 242/max 243）、private bytes 120254464→120139776、working set
  126615552→117518336、thread 12→8。px4-ts合計working setも減少。disposition `安定`。終了後daemon exit 0、
  runtime残留なし、stray px4d/px4-ts 0。
- 原判定: receiver 0–6のCCが非0のため、SPEC 10.2-7の基準ではFAIL（orchestratorの判定も`verdict=FAIL`）。
  px4-userlandは欠落ごとの時刻を出力しないため、欠落が同時刻だったかは本runから判定できない。
- 比較（`px4drv-soak-main1`、2026-10-10 06:55:36〜08:55:53 UTC／15:55〜17:55 JST）: 同じQ3U4・PC・
  受信チャンネル（BS15/TS0 ×4、T22 ×4）で`tsukumijima/px4_drv` WinUSB版（`px4_drv_winusb-260922.zip`、SHA-256
  `1e25e2ea9ac1894ebbe3ba9cb0e18d6beb772f4f42e51a0d1e22bfcaaa2a48ac`、Q3U4の`DeviceInterfaceGUID`をZadig bindingに
  合わせ、`DiscardNullPackets=false`）をBonDriver経由で8受信同時7200秒受信し、リポジトリ外の計測tool
  `bon-ccprobe-ts.exe`（SHA-256 `8225cebf111a7989e4c8a455cfc7ba6192e2c010b6dd658420ebf5134b64d5b1`）で欠落ごとの時刻を
  記録した。burstの出た1 tuner（T1: TEI 11036、CC 620）を除く7 tunerの安定後CCは計53件（S0 6、S1 6、S2 10、S3 10、
  T0 5、T2 8、T3 8）。うち51件は07:47:58.402〜.536 UTC（16:47:58 JST、約134 ms）に全8 tunerで同時に起き、残り2件は
  受信開始約1.3秒後のT2/T3各1件。TEIは0、それ以外の約1時間50分は欠落0。DriverHostのhandle数は全記録点で263、
  終了後processなし。px4_drv側ではCARD APDUを行っていない。計数規則（dup計上、startup除外）はpx4-tsと同一ではない。
- disposition（2026-10-10ユーザー決定、SPEC v0.32 §10.5の0.2.0限定受入判断）: 同一bridge・同一chの2受信で件数が一致する署名と総数（61件 vs 53件）が
  px4_drvと同等であることから、残存CCは2時間に1回程度のhost/USB側の一時停止による環境由来の一斉欠落とみなし、
  **注記付きで受入**とする。原試行のcounterとexit 8は書き換えない。px4-userland側の欠落時刻は未取得であり、
  一斉欠落だったことは件数の並びからの推定である。#50の大量欠落（RAW_IO以前）はこの結果をもって解消と判断する。

### receiver 7

- 短時間matrix（TEI 10953〜10985、CC 558〜638）、soak（TEI 10975、CC 629）、全局確認のT30（TEI 11088、CC 584）・
  T19 rerun（TEI 16069、CC 829）で、receiver 7だけに起動直後の既知形burstを観測した。sync/queue/USB errorは0、
  stream停止・crash・stale leaseなし。
- 同一個体・同一RF・同一PCのpx4_drv WinUSB版2時間でも1 tunerだけに同形のburst（TEI 11036、CC 620）が出た。
  Windows上でのSPEC 10.2.6a比較として、soakのTEIは参照以下、CCは参照より9件多い（計数規則差あり）。
- `e17rest-f83190`（30秒）でもreceiver 7だけTEI 11047、CC 674。
- 2026-10-10ユーザー決定: receiver 7は既知burstとして扱い、0.2.0のブロッカーにしない。v0.1.10限定のdisposition
  （SPEC v0.29 §10.5）は継承せず、SPEC v0.32 §10.5に0.2.0限定の受入判断として記録した。

### claim判定（E17、Windows 11 x64 native libusb、Phase 1）

| model × feature | 判定 | 根拠・範囲 |
|---|---|---|
| Q3U4 tuner（grouping、firmware、ISDB-T/S capture、stop/reopen、USB disconnect/reconnect） | 今回再検証 | `e17r2-fef601`、全局確認、soak。receiver 7は上記扱い |
| Q3U4 card core（ATR、reset、反復APDU、抜去/再挿入、USB再接続後） | 今回再検証 | `e17r2-fef601`、soak中のAPDU 12回 |
| Q3U4 LNB 0/15/0（無負荷開放端） | 今回再検証（receiver 0のみ） | `e17volt-*`。15V保持は30秒、代表負荷は未測定 |
| M1UR/S1UR 同一serial列挙・曖昧指定拒否 | 今回再検証 | `e17multi-c80689` |
| M1UR tuner（ISDB-T/S 0V capture、15V拒否opt-inなし/あり、stop/reopen、daemon再起動、USB切断後の再起動復旧、30分連続） | 今回再検証 | `m1urq-92a667`、`usb-714d78`、`e17m1ur-*`（15V拒否） |
| M1UR card core（ATR、reset、反復APDU、受信中APDU 300/300、抜去/再挿入、USB再接続後） | 今回再検証 | `m1urq-92a667`、`phys-6af665`（card stepのみ）、`usb-714d78` |
| S1UR tuner（ISDB-T capture、ISDB-S拒否、stop/reopen、daemon再起動、USB切断後の再起動復旧、30分連続） | 今回再検証 | `s1urq-81b3aa`、`phys-df3e4e`、`e17s1ur-653101` |
| S1UR card core（ATR、reset、反復APDU、受信中APDU 300/300、抜去/再挿入、USB再接続後） | 今回再検証 | `s1urq-81b3aa`、`phys-df3e4e` |
| M1UR/S1UR same-lease retune | 未認定 | Windows用retune toolなし。stop/reopenと別leaseのT/S切替のみ |
| native-card-adapter（WinSCard/PC/SC） | 対象外（N/A） | Phase 2以降 |

上記4項目およびM1UR/S1UR profile試験の完了により、README・SPEC 10.3・release-validation §0のWindows行を、PX-Q3U4と
PX-M1UR/PX-S1URは`tuner-hardware-verified`／`card-core-hardware-verified`（M1UR/S1URはsame-lease retune未認定を注記）、
その他のmodel/profileは`hardware-unverified`へ更新した。native-card-adapterは全機種N/A。
ready行の非ASCII切断（[Issue #52](https://github.com/Khronos31/px4-userland/issues/52)）は既知の制限として併記した。

### 未実施・逸脱・残課題

- multi〜全局確認のrunはrunごとの新規展開をせずsoakの展開先を再使用した（`e17rest-f83190`、`m1urq-92a667`、
  `s1urq-81b3aa`は新規展開。後二者の物理操作sub-runは親runの展開物を再照合して使用）。
- M1UR/S1URのPC/SC併走APDUは直接`px4ctl card-apdu`で置換（Windows Phase 1にPC/SCなし）。same-lease retuneは
  Windowsで未確認（`retune_tool`はPOSIX IPC専用、`px4-ts`にretuneなし）。同一serial衝突中の`--usb-path`個別起動も未確認。
- `phys-6af665`のUSB stepはorchestratorの誤検出（`px4d --list`のopen_failed）で無効、`usb-714d78`で再実施した。
- E17手順のrunnable setupは`$RT`を事前作成しないが、px4dは存在しない`--runtime-dir`を`NOT_FOUND`で拒否する。
  また同setupはWindows PowerShell 5.1前提（PowerShell 7では`CreateDirectory(path, acl)`が使えない）。手順へ反映した。
- 非ASCII `--runtime-dir`でのpx4d ready行stderr切断（上記、[Issue #52](https://github.com/Khronos31/px4-userland/issues/52)）は0.2.0の既知の制限。0.2.xで修正予定。
- SPEC 10.5-2の再現性確認のうち、両runのrunner image・実効toolchain/build inputの突合は本節の時点で未完了。2026-10-11に実施し一致（上記CI項）。
- 他8 binary archiveの必須短時間matrixは本節の時点で未実施。2026-10-11に実施した（上の2026-10-11節）。
- soakの欠落時刻（px4-userland側）は未取得。#50はopen（RAW_IO以前の大量欠落は解消、2時間1回程度の同時欠落は
  px4_drvでも再現）。M1UR B1のdaemon not-readyは原因未解明。

## 2026-10-09 v0.2.0 candidate（Windows native CI green / hardware-unverified）【不採用】

- 不採用: このcandidate（`8180783`）とその後の`9e2fa3e`は実機E17前後でWindows Q3U4のbridge単位CC欠落（[Issue #50](https://github.com/Khronos31/px4-userland/issues/50)）が判明し、RAW_IO修正を含む`8c40d49`へ置き換えた（上記2026-10-10節）。以下のhash・CI記録は当時のcandidateのもので、0.2.0の最終candidateではない。

- candidate: version `0.2.0`、source commit `818078382df9dec347082d8f2c49ea339ea2d1e6`、branch
  `feat/windows-phase1`。tag・merge・release公開はしていない。
- CI: push run [`37847893047`](https://github.com/Khronos31/px4-userland/actions/runs/37847893047) と
  dispatch run [`37847901323`](https://github.com/Khronos31/px4-userland/actions/runs/37847901323) は全job
  SUCCESS。Windows native job `113553917724`/`113553971785` は6 executable suite、exact release ZIPのCLI
  smoke、fixture cleanupがすべてPASS（fixture length 25、worst worker socket path 74 bytes）。
- artifact: 9 binary archiveとcorresponding-source archiveの計10 archive、外側`SHA256SUMS`（10件）。両runの
  10 archiveはbyte-identicalで、checksumを照合した。外側`SHA256SUMS`も両runで一致。9 binaryのmanifestは
  version `0.2.0`とcandidate `source_ref`を持ち、dependency noticeとembedded audit evidenceも両runで同一。

| 配布binary archive | SHA-256 | 実機status |
|---|---|---|
| `px4-userland-0.2.0-linux-glibc-x86_64.tar.gz` | `e48b8e43687e90650b9a9a089d0b49404aa278eb8c57d45ab154c308353c2986` | 未認定（未実施） |
| `px4-userland-0.2.0-linux-musl-x86_64.tar.gz` | `06e486830e34e311d3f82aabeaca41ef819cec102abe6af9d2ab5b821a2e2538` | 未認定（未実施） |
| `px4-userland-0.2.0-linux-glibc-aarch64.tar.gz` | `02dc1dd27a97e9435d1be0e1e2dde82ac9bd2d45ee5f57c101ab944f112bae6d` | 未認定（未実施） |
| `px4-userland-0.2.0-linux-musl-aarch64.tar.gz` | `b32ffcf8f83d3cba73bf28a501b3b8b285d89c10348ef8d9fe66353c664d9c0b` | 未認定（未実施） |
| `px4-userland-0.2.0-darwin-arm64.tar.gz` | `73ba974ae74233d8432c79acdeaef45dcf1b1058ee64b04427d630629e556421` | 未認定（未実施） |
| `px4-userland-0.2.0-android-aarch64.tar.gz` | `68f52a27cbf09bd3ea99697c1ad9a8dea74586666d35fc50c3100243261e3bba` | 未認定（未実施） |
| `px4-userland-0.2.0-android-armv7a.tar.gz` | `322817f954afa4c2b0b89bf4d5727c17394507d904b2b6c971b6f83bd62375a0` | 未認定（未実施） |
| `px4-userland-0.2.0-android-x86_64.tar.gz` | `1b17b10ea0974d27d8b48e0a78e438e8206e26f397e5757945c1b2f9a563374c` | 未認定（未実施） |
| `px4-userland-0.2.0-windows-x86_64.zip` | `20d16cf733c38bd122fd2d4748512e9948223c059f45a812e8bddbe73e1a9a5e` | 未認定（未実施） |

corresponding-source archive: `px4-userland-0.2.0-source.tar.gz` SHA-256
`cf6778e132fef2a40fd4f38779366e7eb6d9fdfa03958fe2d79b1b651a1b9abf`。

- input: pinnedなlibusb 1.0.30、Android NDK 27.3.13750724、Windows llvm-mingw 20250910 UCRT x86_64、
  workflow/source/build optionsはcandidate commitで一致。ただしrunner上でfloatするartifact生成toolchain
  （compiler/SDK/NDK等）の実効観測値はinventoryしていない。archive bytesは一致したが、SPEC 10.5-2の
  input一致gateは未完了（pending）である。
- shared POSIX影響: Windows Phase 1はPOSIX runtimeを変更した。`posix_ipc.h`/`control_server.h`の
  `NativeHandle`/`PathChar` aliasと`valid()`述語（`>= 0`→`!= kInvalidHandle`）、`control_server.cpp`の
  `empty_path()` helper、`control_workers.h`の`WakeHandle`、`px4d_signals.h`の追加stop API
  （`request_stop`/`notify_cleanup_complete`/`cleanup_complete`。POSIXではflag設定とno-op）がPOSIX buildにも
  及ぶため、「POSIX runtime未変更」とは扱わない。一方、POSIXのUSB transport、TS aggregation/demux、
  firmware framing/hash、sleep timing primitiveは挙動を保持し、sleep呼出しとfirmware file openは`#else`側の
  元のPOSIX codeを維持する。
- 失敗試行: `532acd1` ShellCheck SC2015、`3de7d51` control startup error 255、`94f4f87` 診断で
  default LOCALAPPDATA owner AdministratorsとAF_UNIX OKを確認、`8180783` fixture修正で解消。
- 実機USB/tuner/card/LNBは未実施で`未認定`。soakはユーザー未決定。release未公開。

## 2026-10-09 Windows Phase 1 実装状況（native offline PASS / hardware-unverified）

- 状態: `未認定`（物理機能）。WindowsはSPEC v0.30でPhase 1の対象へ追加した。物理tuner/card/LNB/USBの
  hardware evidenceはなく、`px4-userland-<version>-windows-x86_64.zip`の実機gateは未完了とする。
- cross-build evidence: pinnedなllvm-mingw 20250910 UCRT x86_64（SHA-256
  `f83556c9ffa4d4291fadea1a0776c1383332dacdf4d7fbdf974c2928cb32c6f7`）とpinned libusb 1.0.30 sourceで、
  `px4d.exe`／`px4-ts.exe`／`px4ctl.exe`とlibusb-1.0.dllをクロスビルドし、PE32+ x86_64、import DLLが
  system DLLとlibusbのみであること、source/build path非混入、deterministic zip packagingを確認した。
- **native Windows 11 Pro x64 (build 26300、host `home-pc`、SSH経由)** でoffline/mock testを
  実行し**全PASS (EXIT 0)**:
  - `px4_windows_tests`（既存portable core 44 suite）
  - `px4_windows_platform_tests`（AF_UNIX endpoint/DACL/lease/nonce/pacing）
  - `px4_windows_workers_tests`（AF_UNIX wake、worker wake/completion/rollback）
  - `px4_windows_control_tests`（mock CARD_CONNECT/CARD_TRANSMIT を含む control round-trip）
  - `px4_windows_stdin_tests`（stdin EOF monitor: already-EOF/read失敗/never-closing cancel）
  - `px4_windows_ts_output_tests`（TS binary sink: byte fidelity/broken/stalled cancellation/immediate finish）
  - 3 CLI (`px4d`/`px4-ts`/`px4ctl`) の `--help` が exit 0。
- 未実施: 物理USB/tuner/card/LNBの実機matrix。`hardware-unverified`を維持し、
  `tuner-hardware-verified`／`card-core-hardware-verified`とは表示しない。
- Windows CI jobは未実行（push時に実行予定）。これは本節作成時点の記録であり、上記の「2026-10-09 v0.2.0
  candidate」節でCIを実行して置き換えた。CI成功は実機evidenceではなく`hardware-unverified`を維持する。
- WinSCard互換DLLとMicrosoft PC/SC IFD登録は今回の対象外（Phase 2以降で検討）。

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

### 結果commit・tag CIと公開asset確認

結果commit `7ad5f6691f77a9aa58c4097f8b6dfea77f6d9b6a`へannotated tag `v0.1.10`を付けた。
tag push run [`37662317383`](https://github.com/Khronos31/px4-userland/actions/runs/37662317383)と
main push run [`37662317396`](https://github.com/Khronos31/px4-userland/actions/runs/37662317396)は、
同じ結果commitに対する独立runで、両方18 jobすべて成功。
9 archive本体と外側`SHA256SUMS`は全件byte-identical、外側checksumのSHA-256は
`8ef0dee40a0f01763260b7a8032dc18a6363b059fac9add04b876b9f88d94151`。
実効tool/package inventoryの270 entry（job別compiler・SDK・CMake・apk等）は両runで一致。
Xcode15.4/15F31d、SDK14.5、CMake4.4.3、Ninja1.13.2、NDK/compilerとpinned inputsを確認した。
runner imageは元候補と同じversion集合で、build job別の比較は以下のとおり。

| build job | tag run runner image | main run runner image |
|---|---|---|
| Linux x86_64 | ubuntu-24.04 / 20261004.327.1 | ubuntu-24.04 / 20260927.320.1 |
| Linux aarch64 | ubuntu-24.04-arm / 20260927.135.1 | 同左 |
| macOS | macos-14-arm64 / 20260831.0302.1 | 同左 |
| Android aarch64 | ubuntu-24.04 / 20261004.327.1 | ubuntu-24.04 / 20260927.320.1 |
| Android armv7a・x86_64 | ubuntu-24.04 / 20260927.320.1 | ubuntu-24.04 / 20261004.327.1 |

最終8 binary archiveを元の実機候補run `37602531584`と比較し、全file inventory、type、mode、
link先が一致。file payloadの差はREADME、manifest、内側checksumの3 fileのみ。
manifestは項目別に照合し、差を`source_ref`とREADME file entryのhash/sizeだけに限定できた。
実行ファイル、Termux launcher、IFD、link inventory、その他全payloadはbyte-identical。
各内側checksumが成功し、source archiveのREADME/SPEC/試験記録/手順/VERSION/製品sourceが
結果commitのsnapshotと一致した。実機試験を結果commit後に繰り返した意味ではない。
ローカルの照合証拠は`final-payload-report.json`、`final-tag/`、`final-main/`、`ci-logs/`。

独立した公開前レビューはPROCEED。未解明のMac事象、receiver7、MLT5認定失効、soak省略を
release notesへ明記し、payload照合完了後に公開した。
[v0.1.10 Stable](https://github.com/Khronos31/px4-userland/releases/tag/v0.1.10)は
2026-10-07T17:58:59Zに公開（JST10-08 02:58:59）。Latest、draft=false、prerelease=false。
draft upload後と公開後にそれぞれ9 archive + 外側checksumの全10 assetを再downloadし、
最終tag CI artifactとのbyte一致とchecksum成功を確認した。公開hashの正本はreleaseの
[`SHA256SUMS`](https://github.com/Khronos31/px4-userland/releases/download/v0.1.10/SHA256SUMS)。
この公開後記録はmainへ追記し、既に公開したtagとassetは固定する。

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
