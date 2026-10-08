# px4-userland 仕様

Status: Frozen v0.30 (2026-10-09)

本書の`MUST`、`MUST NOT`、`SHOULD`は規範要件を示す。実機観測で前提の誤りが判明した場合も暗黙に
実装だけを変えず、本書のversionと変更理由を更新してから実装する。

### v0.30 change record (2026-10-09)

- 1、2.1、2.2、2項support matrix、3.1、3.2、7.5、10.3、10.4、11節: Windows 11 x64を製品範囲へ
  追加するPhase 1の要件を定める。Windowsは`px4d`／`px4-ts`／`px4ctl`とversioned local IPCを提供し、
  Q3U4のtunerと内蔵カードリーダーのCARD_*操作を扱う。同一ホスト内限定、AF_UNIX相当のendpoint、
  same-user private access、BCryptGenRandom nonce、cooperative shutdown、binary stdout、pacingを要求する。
  WinSCard互換DLLとMicrosoft PC/SC IFD登録はPhase 2以降の対象外であり本版に含めない。
- 10.3、10.4節: Windowsはbuildとoffline testだけを`build-tested`として扱い、実機hardware evidenceが
  得られるまで`hardware-unverified`を維持する。Windows x64配布物はPhase 1のrelease対象へ追加するが、
  10.5節の必須実機短時間matrixが未完了の間は当該artifact gateを未完了とする。
- 7.5節: Windowsはllvm-mingw/UCRT x64を正規toolchainとし、toolchainとlibusbのversion・checksumを
  build scriptへ固定する。POSIX kernel API・chardev/ioctl・kernel moduleへ依存しない。Windows固有処理は
  platform adapterへ隔離し、portable coreと既存POSIX runtimeの挙動・test coverageを変更しない。
- 10.4、10.5節: Windows Phase 1の追加とrelease artifact集合の拡張を`0.2.0`として公開する。10.5節の
  v0.1.10限定受入判断は0.1.10だけに適用し、0.2.0の受入・matrix・再現性確認へ継承または拡張しない。

### v0.29 change record (2026-10-08)

- 10.5節: v0.1.10の実機観測とユーザー決定に基づく、当該release限定の受入判断を明記する。
  receiver 7の参照比較は全試行で参照以下を証明した扱いにせず、既知不具合を継続する。
  macOSのreceiver 0〜3の単発CC異常は原記録と未解明の原因を保持し、追加の固定20回で
  receiver 0〜6の異常が再現しなかった結果とともに非blockingとして受け入れる。
  TS integrity計数、CLI exit、実装、将来releaseの一般受入条件は変更しない。

### v0.28 change record (2026-10-07)

- 6.5節: `px4-ts`へ`--channel CH`を追加し、mirakcが渡すチャンネル表記からsystem・
  frequency_khz・ISDB-Sのslotを導出する公開CLI契約を定める。`T<NN>`と`<NN>`は地上波物理
  チャンネル13〜62を`395142 + NN * 6000` kHzへ、`BS<NN>`と`BS<NN>_<S>`は奇数トランスポンダ
  01〜23を`1049480 + ((NN - 1) / 2) * 38360` kHzへ、`CS<N>`は偶数トランスポンダ2〜24を
  `1613000 + ((N - 2) / 2) * 40000` kHzへ変換する。`BS<NN>_<S>`はslot S（0〜11）を固定し、
  `BS<NN>`は`--slot`/`--stream-id`を必須、`CS<N>`は既定slot 0で上書き可能とする。
- 6.5節: `--channel`は`--system`/`--frequency-khz`と排他、`BS<NN>_<S>`は`--slot`/`--stream-id`と
  排他、`T<NN>`/`<NN>`は`--slot`/`--stream-id`を受け付けない。接頭辞は大文字、桁数と桁上がりは
  厳密に検査し、これ以外の表記・範囲外はusage error（exit 2）とする。表記は以後互換性を保つ
  公開インターフェースとする。

### v0.27 change record (2026-10-01)

- 10.5-2節: 再現性確認の受入条件を、final candidateと同一source commitに対する独立した2回のclean CI
  candidate run（別run・新規workspace）と、8 binary archive・corresponding-source archiveの9 tar archive
  すべてのSHA-256一致とarchive本体のbyte-identical、および外側`SHA256SUMS`自体のbyte一致（`cmp`）と定める。入力一致は
  pinned分を同一revision/digest、workflow上floatする分を両runの実効toolchain/build inputの観測値一致で
  判定し、実効値が異なるか記録から同一と確認できない比較はinconclusiveとしてmatching pairを取り直す。
  runner image version/IDとcompiler/SDK/NDK等のartifact生成toolchain識別子・build inputをrelease recordに
  残し、runner metadata（label/OS image）とartifact生成toolを区別する。正規化（UUID・署名・timestampの
  マスク等）による合格は認めない。
- `docs/release-validation.md`§2・§5の再現性手順と判定を上記定義へ合わせ、10.5.2→10.5-2の引用誤りを直す。
  これはdarwin-arm64 `px4d`のLC_UUID/署名領域がbuildごとに変わる非決定性を観測したためで、その修正commitに
  対するexact same-commit 2 runの一致確認は`docs/platforms/validation-results.md`に記録する。

### v0.26 change record (2026-10-01)

- PX-M1URとPX-S1URのUSB serialがともに`000000000000001`であることをLinuxとAndroidで観測した。
  従来のserial単独groupingは異機種の観測を`duplicate`へ潰し、選択後もserialだけでUSB候補を再照合する。
  4.1節を機種とserialと観測USB位置に基づく列挙へ改め、曖昧なserial指定はclaim前に失敗させる。
  USB位置は現在の接続の選択子であり、抜き差し後の物理個体IDではない。Q3系の相方を位置の近さから推測しない。
- 4.1・4.6節で`--usb-path`と独立したruntime`--instance`、`--list`の場所・LNB能力表示、単一文書の
  `--list-json`を定める。既存のserial名socketは起動時に選んだUSBに結び付いた従来のendpointとし、
  USBの増減に応じた動的な再解決は行わない。同serialでserial名socketと明示TOKENのsocketが
  同時に存在しないよう、daemonのruntime名前空間を排他制御する。IPC wire形式とobserved serial値は変更しない。
- 4.1・4.2・4.6節で`DeviceProfile.supports_lnb_15v`を機種の静的な対応能力として公開する。
  single receiverの5機種はfalse、その他の11機種は仕様上trueとし、実機認定やdaemonのopt-inとは区別する。
  既定0V・`--allow-lnb-power`・明示的な15V要求という安全境界は変更しない。
- 5.2節でGPIO 11の初期化・確認と15V要求の許可を機種profileの能力で決める。GPIO 2/3/7の初期化順は
  従来のboard layoutごとに維持する。現在の5つのsingle receiver profileはGPIO 11を操作しない。
  single receiver frontendには15V給電経路がないため、将来そのprofileをtrueにする変更は当該経路を
  実装するまで初期化前に`UNSUPPORTED`で拒否する。現行16機種の給電動作は変更しない。
- 10.5.2節のPX-M1UR/PX-S1UR同時接続禁止を、受信・カード・給電を伴う認定とsoakへ明確化する。
  両機種の同一serial衝突をexact candidateで確認するには同時接続が必要なため、read-onlyの
  `--list`/`--list-json`と、実施可能な場合の曖昧serial指定のclaim前拒否だけを例外として許す。
  これは同時運転や受信の認定ではなく、物理接続操作には従来どおり利用者の確認を要する。

v0.25ではStable検証を変更影響による選択制へ統合する。6か月または6回目のStableによるQ3U4の周期再認定を削除し、
long soakは変更・観測異常・未認定claimに基づくtriggerがある場合だけ行う。releaseごとのlong soakはQ3U4・追加profileを
含め最大1つのruntime/access pathに限定し、canaryはcanonical環境ID E01–E17から決定的に選ぶ。検証状態の語彙を
`継承` / `今回再検証` / `未認定` / `対象外`に統一し、環境ledgerと環境別手順は`docs/release-validation.md`へ置く。
Android ad-hoc APKの実機検証はdtv-android所管として本リポジトリのrelease gateから除外する。FD数とRSSは
手順どおり記録し、数値の合否基準は設けない。記録系列が最終観測まで安定化しない持続的な増加を示し、外部要因も
特定できない場合、そのsoakは`判定保留`としてpassにしない。

v0.23では10.5.2のlong soak triggerを、変更が影響するmodel/profile/topologyへ限定する。Q3U4へ
影響しないと立証された追加profile固有の変更だけを理由に、Q3U4の代表2時間soakを要求しない。一方、
Q3U4に影響する変更と周期再認定の2時間soak、追加profileの10.2.7認定、共通実装の影響除外に必要な証拠は維持する
（周期再認定はv0.25で削除。以下はv0.23時点の記録）。

v0.24ではPX-M1URとPX-S1URのcanonical Linux x86_64 profile認定完了を反映する。各runtime/access pathの
hardware claimは10.2.8・10.3と個別の検証記録に従い、未実施のpathへ拡張しない。

### v0.25 change record (2026-10-01)

- 10.5、10.5.2節: 6か月または6回目のStableによるQ3U4周期再認定（旧1057-1059行）を削除する。long soakは
  10.5.1の影響分類、観測された異常、未認定claimのいずれかによるtriggerがある場合だけ行い、releaseあたり
  最大1つのruntime/access pathで実施する。glibc/musl双方をsoakする要件を削除し、両libcの差は10.5.2の
  10分短時間回帰で覆う。canonical環境ID E01–E17とcanary/soakの選定順は`docs/release-validation.md`に定める。
- 10.5節: 検証状態を`継承` / `今回再検証` / `未認定` / `対象外`に統一し、canaryの同一lease retuneは
  IPC/lease/retune、frontend/tune、device identityの変更時に限る。それ以外はoffline CIの成功を記録する。
- 10.2.6a節: 周期再認定時のreceiver 7 fresh比較要求を削除し、変更影響によるtriggerのみ残す。
- 1、2.1、2.2、7.2、10.3節: Android ad-hoc APKを本リポジトリのrelease gateおよびsupport claimから除外し、
  dtv-android所管とする。APK経路の実機試験は本リポジトリのStable gateに含めない。
- 10.5.2節: FD数・RSSは記録と傾向のみとし、数値の合否基準を設けない。記録系列が最終観測まで安定化しない
  持続的な増加を示し外部要因を特定できないsoakは`判定保留`とし、passにしない。

### v0.24 qualification record (2026-09-30)

- PX-M1URとPX-S1UR: exact candidate `2f555ff0542c7a36fb2565b64ebc0703f44a0931`を用いたLatitude 5300 / AnduinOS
  Linux x86_64で10.2.7のcanonical profile認定を完了した。環境別のfeature/path evidenceは
  `docs/platforms/validation-results.md`に記録し、未認定runtime/access pathは引き続き限定表示する。

### v0.23 change record (2026-09-30)

- 10.5.2節: 10.5.1の影響判定をlong soak triggerにも適用する。Q3U4へ影響しないことを根拠付きで示せる
  追加profile固有変更ではQ3U4の代表2時間soakを要求せず、変更対象profileの10.2.7認定と対象機能の
  targeted再検証を要求する。共通実装に触れた場合はQ3U4経路の非影響を記録し、両Linux runtimeで短時間回帰
  を行う。影響不明または回帰失敗なら免除しない。
- 10.5.2節: 6か月または6回目のStable releaseで先に来る周期再認定は、変更影響と独立したQ3U4代表2時間
  soakのtriggerとして維持する。10.2.6aのreceiver 7 fresh比較も維持する（両方ともv0.25で削除）。

v0.22ではsingle receiver機種のLNB 15V給電を対応profileから除外する。PX-M1URの実機開放端測定は
0Vのままであり、参照`px4_drv`ではPX-M1UR・ISDB2056/ISDB2056NのLNB setterが無効、S1UR/ISDBT2071
は地上波専用で、これらの機種のGPIO 11給電制御も有効化されていない。これは実測と参照実装に基づく
サポート判断であり、未実測機種の物理的な回路能力まで証明するものではない。現行v0.1.7の実装はこの
契約に未対応で、T/S兼用single receiverのopt-in時15V要求を受け付けてGPIO 11を書き込むため、修正と
否定系試験が完了するまで当該機種のLNB給電を認定しない。
10.5.2のlong soak条件は変更しない。

### v0.22 change record (2026-09-29)

- 3.1、4.2、5.2、10.2.7節: PX-M1UR、DTV02-1T1S-U、DTV02A-1T1S-UのISDB-S受信は
  LNB 0Vのみを対応範囲とし、15V要求はdaemonの`--allow-lnb-power`の有無にかかわらず、
  LNB用GPIOへの書込み前に`UNSUPPORTED`で拒否する。全single receiver機種ではGPIO 11を操作しない。
  S1UR/DTV03A-1TUはISDB-S要求自体を拒否する。外部給電が必要な設備は別途用意する。
- 4.2節: M1URのカード搭載は部分実測されたため、「カードの有無が不明」から「profile認定までは機種全体の
  hardware-verified claimをしない」へ記述を改めた。S1UR/DTV03A-1TUも同じ認定基準に従う。
- 既存のQ3U4等のLNB対応profileと10.5.2のsoak gateは変更しない。v0.1.7との差分は実装・CI・
  対象実機の再検証を経るまで未解決とする。

v0.21ではStable候補ごとの一律な全環境長時間再試験を廃止し、model/device profile、runtime/access path、
feature/pathごとのqualification evidenceと、影響範囲に応じた継承・失効契約へ置き換える。releaseごとの
最終artifact canary、変更triggerに応じたlong soak、新機種のprofile認定は必須とする。未観測のmodel × runtime ×
access path × featureの組合せを検証済みとは推論せず、既存の品質基準とsupport claimは維持する。

### v0.21 change record (2026-09-26)

- 10.2、10.2.6a、10.2.7、10.3、10.4、10.5節: releaseごとの一律なSCS/HAOS各2時間および全runtime各30分の
  再試験を、証拠軸、影響別の継承・失効、release canary、trigger付きlong soak、周期再認定へ置換した。
- 10.2.7節: 新機種・profileはcanonical Linux x86_64で30分以上認定し、固有runtime/access pathは個別に確認する。
- 10.3節、10.5節: support claimを観測済みのmodel/runtime/access path/featureの範囲に限定し、release recordへ
  evidence lineageとimpact判定を残す。Linux aarch64はbuild-tested / hardware-unverifiedのまま維持する。

v0.20では、macOS（darwin-arm64）のproduction executableへlibusb 1.0.30を静的リンクする。v0.19までは
host-provided dynamic libusbを意図しており、配布した`px4d`がHomebrewのlibusb dylibを要求していた。macOS利用者に
Homebrew導入を求めないよう、LinuxおよびAndroidと同じ固定source・checksumのlibusbを使い、7.3節の規定どおり
exact source、license、notice、build/relink obligationsへ切り替える（7.3節、10.4節）。

### v0.20 change record (2026-09-26)

- 7.3節、10.4節: macOS production executableはlibusb 1.0.30を静的包含し、`otool -L`でlibusb dylibと
  macOS system（`/usr/lib/`、`/System/Library/`）以外のdependencyを持たないことを検証する。IFD bundleはlibusbを
  linkしないまま変えない。libusbのexact source、notice、relink instructionsはLinuxと同じ対応source archiveで提供し、
  relinkはmacOS上でもCIで検証する。
- 10.4節: Linux、macOS、Androidの全binary archiveに、検証済みlibusb 1.0.30のexact `libusb/COPYING`を含める。
  binary noticeでこの同梱を明示し、別個のcorresponding-source archiveはexact sourceとbuild/relink materialsを提供する。

v0.19では、同一lease内の再選局（same-lease retune）を許す。`STOP_STREAM`で`consumed`になったleaseでも
`TUNE`成功後に`START_STREAM`を再実行できる。wire形式は変えない。`ATTACH_STREAM`のnonceは`ACQUIRE`が
発行した値をlease内で使い回し、tokenの有効性はarm（`START_STREAM`から`ATTACH_STREAM`または5秒満了まで）
単位で成立する。各armは1回のattachmentだけを受け付け、5秒窓と合わせて期限切れ・再利用を拒否する（6.3節）。

### v0.19 change record (2026-09-25)

- 6.3節: 同一lease内の再選局を追加。`TUNE`成功で`consumed`を`none`へ戻し（re-arm）、`START_STREAM`を再実行
  可能にする。nonceはlease内で不変とし、token有効性はarm単位と定義した。wire形式・protocol versionは不変。

### v0.18 change record (2026-09-25)

12 USB IDをdevice profileに追加した。Q3U4系はbridge数に基づく既存経路へ割り当てる。MLT系はモデル別のI2C
bus/address/TS portとport由来のwire tagを使用する。1 receiver機種はTC90522/R850/RT710経路とplain TS同期を
実装した。追加機種の実機frontend、firmware、card reader、streamとpower sequenceは未検証である。

v0.17では、接続中の対象筐体を列挙する`px4d --list`を追加する（4.6節）。v0.16まで、利用側が`px4d --device`へ
渡す筐体識別子を得るには、4.1節のUSB IDと識別子規則を自前で持ち、sysfs等から組み立てるしかなかった。
対象機種が増えるたびに利用側の表も更新が要り、4.1節の規則とずれる余地がある。`--list`は`px4d`が
既に持つ通常列挙とgroupingをそのまま使い、所有もfirmware loadも行わずに筐体、機種、状態、4.2節の
receiver表を出力する。あわせて通常列挙を訂正し、openまたはdescriptor取得に失敗したデバイスを、読めていない
serialから`invalid_serial`とせず`open_failed`として報告する（`px4-usb-probe`の出力も同様に変わる）。
device contract、IPC、`px4ctl`/`px4-ts`の挙動は変更しない。
あわせてStableリリース基準を改訂する。手持ちの機種では10.2節の厳しい受入試験を行うが、持っていない機種は未検証であることをREADMEの対応機種一覧へ明記した上でリリースする。テスタが現れた機種は、負担にならない範囲の実機検証を依頼し、その報告をもって検証済みへ更新する。Beta公開は対応機種の追加には使わず、機能追加など不安定な変更に限定する。
v0.16では、対象機種にPLEX PX-MLT5PE（`0511:024e`）とe-Better DTV02A-5TS-P（`0511:924e`）を追加する。
`tsukumijima/px4_drv`はDTV02A-5TS-PをPX-MLT5PEのリブランド品として扱い、両者の差分はUSB product IDだけで
ある（driver commit `72a807de2009c2ce376953c75687b4d45708f00e`、winusb commit
`55a02d8246f1011e522fe574599d28b63ceccf0b`）。両機種は1つのIT930xに5つのCXD2856ER/CXD2858ERを持ち、
各receiverがISDB-TとISDB-Sのどちらにもtuneできる単一USBデバイス構成である。この差を扱うため、
4.1節の識別、4.2節のreceiver番号、4.4節のTS tag、5.2節の電源、6.4節のLIST/STATUS値域、7.2節の
1 fd起動経路を機種別に定める。IPCはQ3U4のbyte列を一切変えずに値域だけを拡張し、protocol minorは
変更しない（6.4節）。実機回帰はDTV02A-5TS-PでLinux x86_64 nativeで行う。
v0.15では、Termuxランチャーの終了処理を改訂する。v0.14の猶予（約2秒）では、tune（最大30秒）やカードAPDU（最大3秒）の処理中にSIGKILLが送られる可能性があったため、stage 0によるプロセスグループ回収（drain）を固定40秒の有限猶予へ変更する。猶予超過時はSIGKILLを用い、graceful cleanup、LNB 0V、およびruntime endpoint削除を保証できない旨を警告する。また、source archive監査で `__pycache__/`、`.pyc`、`.pyo`、`.pyd` を拒否する。
v0.14では、Androidの全ABIで共通して不足していたTermux用の正式な2 FD起動経路を追加する。`px4-termux`をstage 0の監督プロセスとして残し、`util-linux`の`setsid`で最初の`termux-usb`を独立したプロセスグループとして起動する。stage 0はそのグループを監督し、stage 1およびstage 2は`exec`で1回につき1 FDを渡す入れ子の`termux-usb`を経て`px4d`へ引き継ぐ。これによりQ3U4の2つのUSBデバイスを1つの`px4d`へ渡し、x86_64をaarch64およびarmv7aと同じ配布候補に加える。
v0.13では、Android NDK API 24のx86_64をCIでcompile/ELF verifyするbuild-only経路として追加する。これは
runtime supportや配布対象ではなく、Bliss OSでの実機試験を行うまではhardware-unverifiedとする。
v0.8では、`empty_intervals`の訂正でstream starvationを見逃さないよう、1秒以下の観測間隔と連続5秒以内の
packet/byte進行を受入条件に追加した。これはwire semanticsの変更ではなく、v0.7のacceptance erratumを
機械的に検証可能にする訂正であり、protocol minorは変更しない。
v0.12ではLinux production executableをlibusb 1.0.30包含のmusl完全静的ELFへ変更し、IFD Handlerをglibc 2.31
またはmuslのhost-loadable shared objectとして分離する。配布archive名はlibc-qualifiedとし、generic Linux名は廃止する。
（履歴）v0.11ではLinux aarch64のnative CI buildとmusl-dynamic配布archiveを追加した。v0.12で配布方式を置換済みで、
build-tested / hardware-unverifiedと表示し、Stable受入では既知の非ブロッカーとして扱う。
（履歴）v0.10ではStable受入方針を、当時の主環境HAOSでの2時間試験と各対象環境での30分以上の実機試験へ改訂した。
receiver 7の既知burstは、同一個体・同一条件で取得した`tsukumijima/px4_drv`の参照結果より悪化しないことを
確認できれば非ブロッカーとする。LNBは無負荷の0V/15V/0V切替を受入済みとし、代表負荷時の能力未確認は
既知制限として記録する。最終配布archiveそのものの試験、重大な未解決issueがないこと、公開前レビューを
Stable公開の条件に追加する。
v0.9では、`v0.1.0 Beta`の実機試験結果と公開時の扱いを明記した。receiver 7の既知burstは同一個体・同一条件の
参照結果と比較して扱う。Beta公開の目的は、追加個体および追加環境の証拠収集とする。
v0.7では、実機長時間試験で確認した`empty_intervals`の意味をUSB待機のTIMEOUT/空completion回数と明記し、
非zero値だけをTS integrity failureにしない受入条件へ訂正した。
v0.6では、LNB 15Vを明示的に許可したdaemonだけが出力できる安全境界、GPIO完了が曖昧な場合の
cleanup debt、片側USB切断時の生存bridge停止、無負荷電圧と負荷時能力を分ける受入条件を追加した。
v0.5では、GPL/LGPLのrelease contractを明確化し、v0.4からのlegacy source cleanupを完了した。製品対象は
Linux（カーネルドライバを導入できない環境を含む）、AndroidのTermuxおよびAPK経路、macOSに限定する。Windows
runtime、adapter、実機受入、配布物、Windows build-only gateは対象から外した。
対象に残る4 runtime経路では、Q3U4のチューナーと内蔵カードリーダーの両方を成立条件とする。

## 1. Objective

`px4-userland`は、対応PX4機種をカーネルモジュールなしで制御するユーザー空間ドライバである。
USB通信にはlibusb-1.0だけを使用し、Q3U4の8チューナーと内蔵ICカードリーダーを同じデバイス所有者の下で扱う。
v0.18ではPX-W3PE4/5、PX-Q3PE4/5、PX-MLT8PE3/5、DTV02A-4TS-P、PX-M1UR、PX-S1UR、DTV03A-1TU、
DTV02-1T1S-U、DTV02A-1T1S-Uを識別対象へ加える。12機種はhardware-unverifiedである。

対象環境はLinux/glibc、Linux/musl、Android/Bionic、macOS、およびWindows 11 x64（Phase 1）とする。
ビルド成功と自動試験成功を移植性の条件とし、実機試験を行っていないOSは、
公開時に「build-tested / hardware-unverified」と明記し、動作確認済みとは表現しない。
WindowsはPOSIX runtimeと同一のversioned IPC wire形式と`control.sock`/`stream.sock`概念を用い、
tunerと内蔵カードリーダーのCARD_*操作を扱う。WinSCard互換DLLはPhase 2以降とし、本版に含めない。

support matrixと実機検証経路は次のとおりとする。

| Environment | Machine | Access path | Primary validation |
|---|---|---|---|
| Linux x86_64 | Dell Latitude 5300 / AnduinOS | native libusb | tuner、card core、PC/SC adapter |
| Linux aarch64 | GitHub Actions `ubuntu-24.04-arm` | native arm64 Alpine/musl CI | build-tested / hardware-unverified |
| HAOS SCS native | Lenovo ThinkCentre M720q / Studio Code Server上のDebian 13/glibc、HAOS kernel | native libusb | SCS directのtuner/card/PCSC経路 |
| HAOS Alpine add-on | Lenovo ThinkCentre M720q / Supervisor add-on | add-on内のmusl/libusb | Alpine/musl add-onのtuner/card/PCSC経路 |
| Android | Pixel 9a / aarch64 / Termux | `termux-usb`および`px4-termux`による2 fd渡し | 正式launcherの実機回帰（Bionic CLI、portable IPC） |
| Android | Google TV Streamer / armv7a / Termux | `termux-usb`および`px4-termux`による2 fd渡し | 正式launcherの実機回帰（Bionic CLI、portable IPC） |
| Android | Bliss OS / x86_64 / Termux | `termux-usb`および`px4-termux`による2 fd渡し | 正式launcherの実機回帰（Bionic CLI、portable IPC） |
| Android | Google TV Streamer / ad-hoc APK | Android USB Host APIからfd渡し | 対象外（dtv-android所管。本リポジトリのrelease gateに含めない） |
| macOS | Apple Mac mini / M2 | native libusb | tuner、card core、PC/SC adapter |
| Windows 11 x64 | GitHub Actions `windows-2022`（test OS）/ Windows 11 x64（supported） | native libusb（`libusb` WinUSB backend） | cross-build確認済み。offline testはWindows native CIで実行。実機`hardware-unverified` |

## 2. Scope

### 2.1 Goals

- Q3U4/W3U4系、MLT系、およびsingle-receiverの各supported profileについて、profile receiver数に応じてtuner制御する。
- v0.18で識別対象に加えたmodelの実機認定状態は4.1表と10.2.8のevidenceに従い、機種単位で判定する。
- PX-Q3U4内蔵ICカードリーダーでカード検出、ATR取得、リセット、T=1 APDU送受信を行う。
- Q3U4を構成する2つのIT9305Eを同一筐体として対応付け、2基間で連動するbackend powerを一貫して管理する。
- USB列挙、制御転送、非同期TS転送、カードUARTをlibusb-1.0で実装する。
- Androidでは、アプリが開いたUSB file descriptorを`libusb_wrap_sys_device()`で受け取る。
- portable coreをC++17と標準ライブラリで実装し、OS固有処理をplatform adapterへ隔離する。
- muslおよびBionicでビルドし、glibc固有APIやGNU拡張へ依存しない。
- 派生コードのライセンスをGPL-2.0-onlyとする。
- Linux/macOSではPC/SC IFD Handlerを提供し、Androidではportable IPCを公開する。
- Linux、Android Termux、macOSの各runtime経路でチューナーと内蔵カードリーダーを扱う。Android ad-hoc APKの
  実機検証はdtv-android所管とし、本リポジトリの配布物・release gateには含めない。
- Windows 11 x64で`px4d`／`px4-ts`／`px4ctl`とversioned local IPCを提供し、tunerと内蔵カードリーダーの
  CARD_*操作を扱う。同一ホスト内限定のAF_UNIX相当endpoint、same-user private access、
  BCryptGenRandom nonce、cooperative shutdown、binary stdout、高分解能pacingを満たす。
- Windows固有処理（socket、endpoint権限、非同期wake、nonce、sleep、console/signal、stdout）を
  platform adapterへ隔離し、portable coreとPOSIX runtimeの実装・test coverageを変更しない。
- 最終ツリーから、カーネルモジュール、DKMS、カーネル用chardev、非対象機種、旧Windows専用ホストなど、
  portable Q3U4 userland実装に不要なコードと配布処理を削除する。

### 2.2 Non-goals

- `tsukumijima/px4_drv`へのPull Request。
- ファームウェアバイナリの同梱。
- ファームウェアのダウンロード、vendor driverからの抽出、変換機能。
- mirakcの同梱またはmirakc側の変更。
- Home Assistantアドオンの作成または変更。
- PX-MLT5U、ISDB6014、その他v0.18の4.1表にない機種の動作保証。
- v0.18で追加した12機種は実装・識別対象であり、全体を一律にhardware-verifiedとは扱わない。
  機種profileの認定状況とruntime/access path別claimはREADMEとevidence recordに明記する。
- 配布用Android APKへの統合。ad-hoc APKによる実機検証は本リポジトリのrelease gateではなくdtv-android側で扱い、
  本リポジトリのrelease artifactと検証gateにAPKを含めない。
- B-CAS/ACASの暗号処理、ECM処理、TSのスクランブル解除。
- ネットワーク越しの利用。IPCは同一ホスト内に限定する。
- Windows向けWinSCard互換DLL、Microsoft PC/SC IFD登録、System32 WinSCardへの転送、x86（32-bit）配布物。
  これらはPhase 2以降の対象とし、Phase 1の`px4d`／`px4-ts`／`px4ctl`とlocal IPCには含めない。
- Windowsでのカーネルドライバ／WinUSB INF配布、kernel-mode component。WindowsのUSB accessはlibusb経由に限定する。

Q3U4またはMLT5系と共通するチップを持つ他機種で偶然動作しても、対応機種一覧へ追加しない。
実機確認、回帰試験、明示的な仕様変更を行うまでは「unsupported / unverified」とする。

## 3. Product architecture

### 3.1 Process model

Q3U4のUSBデバイスを直接所有するプロセスは、長寿命の`px4d`ただ1つとする。

Q3U4は同一筐体内に2つのIT9305Eを持ち、各USBデバイス上で複数チューナーと制御経路を共有する。
チューナーごとに独立したプロセスがlibusb interfaceをclaimする構成は採用しない。
MLT5系は1つのIT930xだけを持つ単一USBデバイスであり、`px4d`はその1デバイスを同じ規則で所有する。

提供する実行ファイルは次の3つとする。

- `px4d`: USBデバイス、ファームウェア、受信機、TS転送、共有電源、カードセッションを所有する。
- `px4-ts`: `px4d`へ接続し、1受信機を確保してMPEG-TSをstdoutまたは指定ファイルへ出力する。
- `px4ctl`: デバイス一覧、状態、統計、カード状態、ATR、reset、APDU送受信を扱う診断・制御CLI。

`px4d --list`は例外として何も所有せず、接続中の対象筐体を列挙して終了する（4.6節）。

`px4d`はforeground動作を標準とし、自身でdaemonizeしない。プロセス監視は利用側へ委ねる。
LNB 15Vは対応profileでも安全上の明示的opt-inとし、`px4d --allow-lnb-power`なしで受けた15V要求は、
LNB用GPIOを書き込まず`UNSUPPORTED`で拒否する。対応しないprofileではopt-in時も拒否する。
0V要求と地上波利用はこのoptionに依存しない。

### 3.2 Internal layers

最終実装は以下の責務へ分割する。

1. `usb`: libusb context、列挙、fd wrap、interface claim、control/bulk transfer、hotplug。
2. `it930x`: firmware load、register、I2C、TS aggregation、UART、GPIO。
3. `frontend`: TC90522、R850、RT710、ISDB-T/S tune、C/N、LNB。
4. `device`: Q3U4の2 USBデバイスの対応付け、8受信機、共有電源、寿命。
5. `stream`: 非同期bulk transfer、188-byte packet同期、受信機別分配、queue、統計。
6. `card`: card hardware adapter、ATR、T=1状態機械、APDU、共有・排他。
7. `ipc`: versioned local protocol、control connection、TS data connection。
8. `platform`: filesystem Unix domain socket（POSIX）またはAF_UNIX相当socket（Windows）、
   終了通知、nonce、sleepなどのOS差。Windows実装は`userland/src/windows/`へ置く。
9. `cli`: `px4d`、`px4-ts`、`px4ctl`。

portable coreからOS vendor固有header、Linux kernel header、glibc内部APIをincludeしない。

### 3.3 Build system and language

- CMake 3.16以上を正規ビルドシステムとする。
- portable coreはC++17とする。
- チップ制御コードをCから再利用する場合はC11とし、Linux kernel型・macro・allocatorを含めない。
- portable coreは例外とRTTIを使用せず、失敗を固定enumまたはresult型で返す。標準thread、mutex、
  condition_variable、atomicは使用できる。
- 内部エラーは独自の固定enumを使用し、Linuxの負のerrnoを公開IPCへ直接流さない。
- libusbの最小バージョンは1.0.23とする。
- Androidのruntime targetはAPI 24以上、`armv7a-linux-androideabi`、`aarch64-linux-android`、
  `x86_64-linux-android`とする。3つのABIを同一の配布・監査対象とし、各archiveへ同じ`px4-termux`を収録する。
- Windows targetはx86_64のみとし、llvm-mingw/UCRTでクロスビルドする。toolchainとlibusbのversion・
  SHA-256をbuild scriptへ固定し、PE architecture、import DLL、build path leak、legacy artifactを監査する。

## 4. Device contract

### 4.1 Q3U4 identification and grouping

- 対応USB IDは`0511:084a`だけとする。
- 同じ物理Q3U4に属する2つのUSBデバイスをserial情報で対応付ける。
- `dev_id == 1`を主デバイスとし、物理カードスロットは主デバイス側の1つだけ公開する。
- 対応する2デバイスが揃わない場合は、完全なQ3U4としてreadyにしない。
- 複数筐体を列挙できる設計にするが、1つの`px4d`インスタンスが所有するのは`--device`で選択した1筐体とする。
- 同一USB interfaceにカーネルドライバまたは別プロセスが接続中なら、暗黙に奪わず`busy`で失敗する。

v0.18の対応USB IDと筐体識別子は次のとおりとする。未掲載のproduct IDは`unsupported`とする。

| Model | USB ID | USB devices | Instance identifier | Receivers | Card reader |
|---|---|---:|---|---:|---|
| PX-Q3U4 | `0511:084a` | 2 | 15桁serialの末尾`1`/`2`を除いた14桁base serial | 8 | built-in |
| PX-Q3PE4 | `0511:024a` | 2 | 同上 | 8 | hardware-unverified |
| PX-Q3PE5 | `0511:074a` | 2 | 同上 | 8 | hardware-unverified |
| PX-W3U4 | `0511:083f` | 1 | 15桁10進数字のUSB serial全体 | 4 | built-in |
| PX-W3PE4 | `0511:023f` | 1 | 同上 | 4 | hardware-unverified |
| PX-W3PE5 | `0511:073f` | 1 | 同上 | 4 | hardware-unverified |
| PX-MLT5PE / DTV02A-5TS-P | `0511:024e` / `0511:924e` | 1 | 15桁10進数字のUSB serial全体 | 5 | built-in |
| PX-MLT8PE3 | `0511:0252` | 1 | 同上 | 3 | hardware-unverified |
| PX-MLT8PE5 | `0511:0253` | 1 | 同上 | 5 | hardware-unverified |
| DTV02A-4TS-P | `0511:0254` | 1 | 同上 | 4 | hardware-unverified |
| PX-M1UR | `0511:0854` | 1 | 同上 | 1 | built-in |
| PX-S1UR | `0511:0855` | 1 | 同上 | 1 | built-in |
| DTV03A-1TU | `0511:0052` | 1 | 同上 | 1 | hardware-unverified |
| DTV02-1T1S-U | `0511:004b` | 1 | 同上 | 1 | built-in, hardware-unverified |
| DTV02A-1T1S-U | `0511:084b` | 1 | 同上 | 1 | built-in, hardware-unverified |

- 単一USB device機種のUSB serialはASCII数字15桁でなければならない（DTV02A-5TS-P実機観測値の形式）。それ以外は
  `invalid_serial`とする。MLT5系ではserial末尾を`dev_id`として解釈しない。
- MLT5系のtopologyとspeedはQ3U4と同じ条件（interface 0 alt 0、bulk endpoint `0x81`/`0x02`/`0x84`/`0x85`、
  各512 byte、high speed以上）を要求する。
- `--device`、PC/SCの`device=`、runtime instance名は、14桁（2 bridge機種）または15桁（単一USB機種）のASCII数字だけを受理する。
  桁数で機種群を判別できるため、Q3U4 base serialとMLT5系serialは衝突しない。
- 単一USB機種は`dev_id 1`として扱い、物理カードスロットを1つ公開する。カードreader有無がupstreamで確認できない機種は
  card reader hardware-unverifiedとする。

v0.26では、上記のserialは**観測された値**であり、全機種を通じて一意な筐体IDではない。
上記の「runtime instance名は数字だけ」の規則を次のように改訂する。

- groupingは機種とserialを併用する。単一USB機種は同機種・同serialの複数観測もそれぞれ1筐体として保持し、
  USB位置で区別する。2 bridge機種は同機種・同base serialのdev 1とdev 2が各1件のときだけreadyにする。
  片側欠落はincomplete、いずれかが複数ならduplicateとして全候補を診断に残す。位置の近さから相方を推測しない。
- `px4d --device SERIAL`は従来どおり14桁または15桁の数字を受け付ける。native列挙で同じserialに
  readyな筐体が複数あるか、該当するduplicate群がある場合は、任意の1台を選ばずclaim前にusage error
  （exit 2）とし、候補の機種・bus・address・portと`--usb-path`指定をstderrへ示す。
  対象が1筐体のときだけserial単独で開く。
- native起動では`--device SERIAL`に加え`--usb-path PATH`を1回または2回指定できる。
  `PATH`は現在のUSB接続を表す`BUS:ADDRESS`（各1..255）または`BUS-PORT[.PORT...]`
  （BUSと各PORTは1..255、PORTは1..8要素）であり、後者は`--list`の`port`と同じ表記とする。
  単一USB機種は1パス、2 bridge機種はdev 1/dev 2の2パスを要求する。serial・機種・bridgeの整合、
  パスの一意性、観測した場所の存在をclaim前に検査する。一致しない、または場所が取得できない場合は失敗し、
  未知の場所を推測で補わない。`--fd`と`--usb-path`は併用しない。FD経路は渡されたFD自体を選択子とする。
- 選択したgroupと実際にopen/claimするUSB候補は同じ列挙オブジェクト、FD経路では同じFD indexで対応付ける。
  serialだけで候補を再照合してはならない。2 bridgeの明示パスも、指定順ではなく観測されたdev 1/2に配置する。
- runtime endpointの`instance`はobserved serialとは別の単一パス要素とする。`--usb-path`指定時は
  `px4d --instance TOKEN`を必須とし、TOKENは1..80文字のASCII英数字・`_`・`-`・`.`だけを受理する
  （`.`と`..`単体、および他機種のserialとの混同を避けるため14桁または15桁の数字列は不可）。
  FD起動でも`--instance`を任意に指定できる。
  省略時は従来どおりobserved serialを使う。既存socketに結び付いたUSBは後続のhotplugでも切り替えない。
  同じTOKENの2 daemonは同じsocketを共有せず、後から起動した方がbusyで失敗する。
  `px4ctl`・`px4-ts`の`--instance TOKEN`とPC/SCの`instance=TOKEN`は従来のserial指定と排他的な
  endpoint指定とし、observed serialをIPC応答で保持する。クライアントのserial指定は既存socketへ接続する
  互換経路であり、現在接続中のUSB全体を再列挙して一意性を判定するものではない。
- 同じruntime rootとobserved serialを使うdaemon群は、endpoint公開前から終了後のsocket解放まで
  serial名前空間の排他を保持する。serial名instanceは排他、明示TOKENのinstance同士は共有とし、
  両modeが混在する起動は`BUSY`で拒否する。これにより、明示TOKENのdaemonが1台でも稼働中なら
  新規のserial名socketは作られず、旧`--device SERIAL`クライアントが別の稼働中機器へ誤接続しない。
  排他は同じruntime rootと同一UIDを使うv0.26以降のdaemon間で成立し、異なるroot・異なるUID・
  旧版daemonとの混在は対象外とする。
  ロックに使うruntime root内のファイルは通常終了後に最後の保持者が安全に削除する。
  競合するopen済みinodeとpathnameの一致を検証し、unlink後の古いinodeへロックを得たプロセスは
  再取得する。ロック取得失敗時にendpointを公開しない。
- USB位置とTOKENは接続中の選択・接続先指定に使う。抜き差し・ポート交換後も同じ物理個体を追跡する保証はなく、
  UUIDを自動発行しない。15V能力は機種profileの値であり、USB位置やTOKENから配線状態は推定しない。

`DeviceProfile.supports_lnb_15v`は機種の**仕様上の15V対応能力**を表す真偽値とする。
PX-M1UR、PX-S1UR、DTV03A-1TU、DTV02-1T1S-U、DTV02A-1T1S-Uはfalse、
4.1表の他の11機種はtrueとする。falseのうちPX-M1URとDTV02の2機種はISDB-Sの0V受信に対応し、
PX-S1URとDTV03A-1TUはISDB-T専用である。trueは実機検証済み、現在の給電許可、実際の出力電圧を意味しない。

### 4.2 Receiver numbering

受信機番号は再接続後も次の順序を維持する。

| ID | Main/sub device | System |
|---:|---|---|
| 0 | `dev_id 1`, receiver 0 | ISDB-S |
| 1 | `dev_id 1`, receiver 1 | ISDB-S |
| 2 | `dev_id 1`, receiver 2 | ISDB-T |
| 3 | `dev_id 1`, receiver 3 | ISDB-T |
| 4 | `dev_id 2`, receiver 0 | ISDB-S |
| 5 | `dev_id 2`, receiver 1 | ISDB-S |
| 6 | `dev_id 2`, receiver 2 | ISDB-T |
| 7 | `dev_id 2`, receiver 3 | ISDB-T |

MLT5系のreceiver番号は次のとおりとする。各receiverはtuneごとにISDB-TまたはISDB-Sを選択でき、
同じleaseのままretuneでsystemを切り替えられる。streamおよびstop時のsystemは直前に成功したtuneのsystemとする。

| ID | Device | Local | System | px4_drv入力（I2C bus, demod address, TS port） |
|---:|---|---:|---|---|
| 0 | PX-MLT5PE / DTV02A-5TS-P | 0 | ISDB-T/ISDB-S | bus 3, `0x65`, port 0 |
| 1 | 同上 | 1 | ISDB-T/ISDB-S | bus 1, `0x6c`, port 1 |
| 2 | 同上 | 2 | ISDB-T/ISDB-S | bus 1, `0x64`, port 2 |
| 3 | 同上 | 3 | ISDB-T/ISDB-S | bus 3, `0x6c`, port 3 |
| 4 | 同上 | 4 | ISDB-T/ISDB-S | bus 3, `0x64`, port 4 |
| 0 | PX-MLT8PE3 | 0 | ISDB-T/ISDB-S | bus 3, `0x65`, port 0 |
| 1 | 同上 | 1 | ISDB-T/ISDB-S | bus 3, `0x6c`, port 3 |
| 2 | 同上 | 2 | ISDB-T/ISDB-S | bus 3, `0x64`, port 4 |
| 0 | PX-MLT8PE5 | 0 | ISDB-T/ISDB-S | bus 1, `0x65`, port 0 |
| 1 | 同上 | 1 | ISDB-T/ISDB-S | bus 1, `0x64`, port 1 |
| 2 | 同上 | 2 | ISDB-T/ISDB-S | bus 1, `0x6c`, port 2 |
| 3 | 同上 | 3 | ISDB-T/ISDB-S | bus 3, `0x6c`, port 3 |
| 4 | 同上 | 4 | ISDB-T/ISDB-S | bus 3, `0x64`, port 4 |
| 0 | DTV02A-4TS-P | 0 | ISDB-T/ISDB-S | bus 3, `0x65`, port 0 |
| 1 | 同上 | 1 | ISDB-T/ISDB-S | bus 1, `0x6c`, port 1 |
| 2 | 同上 | 2 | ISDB-T/ISDB-S | bus 1, `0x64`, port 2 |
| 3 | 同上 | 3 | ISDB-T/ISDB-S | bus 3, `0x64`, port 4 |

1受信機は同時に1クライアントだけが占有できる。他受信機とカードは並行利用できる。

1 receiver機種は単一TSをplain sync (`0x47`) で同期し、wire tag demuxを行わない。

| Model | Frontend構成 | System | 初期化差分 |
|---|---|---|---|
| PX-M1UR | TC90522(T/S)+R850+RT710 | T/S切替 | 標準TC90522 T/S |
| DTV02-1T1S-U | TC90522(T/S/S0)+R850+RT710 | T/S切替 | ISDB2056 S/S0配線 |
| DTV02A-1T1S-U | TC90522(T/S/S0)+R850+RT710 | T/S切替 | ISDB2056N secondary SとS0固有設定 |
| PX-S1UR | TC90522(T)+R850 | Tのみ | PXS1UR_MODEL |
| DTV03A-1TU | TC90522(T)+R850 | Tのみ | ISDBT2071_MODEL、TC90522 T address `0x18` |

S1UR/ISDBT2071のISDB-S要求はreceiver capability検査で拒否する。T/S切替機種のLNBは0Vを既定とし、
15V対応profileでは`--allow-lnb-power`が指定された場合だけ明示的なISDB-S 15V要求を許可する。
PX-M1UR、DTV02-1T1S-U、DTV02A-1T1S-UはISDB-SのLNB 0V受信のみを対応範囲とし、15V要求は
opt-inの有無によらず、LNB用GPIOへの書込み前に`UNSUPPORTED`で拒否する。frontendとcardは共通
backend-power referenceを使う。
PX-M1URとPX-S1URはcanonical Linux x86_64のprofile認定を完了した。DTV03A-1TUはprofile認定が完了するまで
hardware-unverifiedとし、各runtime/access pathのclaimは10.2.8の独立evidenceに従う。

MLT8PE3は3 receiver、DTV02A-4TS-Pは4 receiver、MLT8PE5は5 receiverで、各receiverのsystem capabilityは
ISDB-T/ISDB-Sとする。1 receiver機種のsystem capabilityは次のとおりとする。

| Models | Receiver IDs | System |
|---|---|---|
| PX-M1UR, DTV02-1T1S-U, DTV02A-1T1S-U | 0 | ISDB-T/ISDB-S |
| PX-S1UR, DTV03A-1TU | 0 | ISDB-T |

1 receiver機種はreceiver 0だけを使う。canonical profile認定未完了の機種はhardware-unverifiedである。

### 4.3 Tuning API

公開するtune parameterは固定幅で、少なくとも次を含む。

- system: `ISDB_T`または`ISDB_S`
- frequency: kHz単位の`uint64`
- stream ID: ISDB-SのTSID、または明示的に指定したslot
- bandwidth: ISDB-Tでは6MHz
- LNB voltage: 0Vまたは15V

slotとTSIDはIPC上で別fieldとし、値の大きさから暗黙判定しない。

### 4.4 TS stream

- 1パケットを188 byteとして扱う。
- aggregation tagは筐体全体のreceiver IDではなく、各IT9305E内で独立した`1..4`である。
- wire上の同期byteは`(local_receiver_tag << 4) | 0x07`、すなわち`0x17`、`0x27`、`0x37`、`0x47`だけを
  受理する。bit 7が立つ値はtransport error indicatorとして拒否する。
- global receiver IDは`((dev_id - 1) * 4) + (local_receiver_tag - 1)`で求める。したがって同じ`0x17`でも、
  `dev_id 1`ではreceiver 0、`dev_id 2`ではreceiver 4へ分配する。
- MLT系のaggregation tagは単一IT930x内の`1..receiver_count`であり、対応する`0x17`から`0x57`までだけを受理する。
  1 receiver機種は`0x17`だけを受理する。新機種でこのtag形式が一致するかはhardware-unverifiedである。
  global receiver IDは`local_receiver_tag - 1`とする。Q3U4の受理集合は変更しない。
- 受信機へ分配するとき、出力TSのsync byteを`0x47`へ戻す。
- tune、stop、再openの境界で前回TSの端数とqueueを破棄する。
- queue overflow、bulk transfer failure、同期喪失をsilent dropにせず、統計と終了理由へ反映する。
- `px4-ts`のstdoutにはTS以外を書かず、診断はstderrへ出す。

### 4.5 Firmware

- `px4d --firmware PATH`を必須とする。
- 同じfirmwareを両方のIT9305Eへ必要に応じてloadする。
- v0.4で受理するIT930x firmware imageは、長さ2,169 byte、SHA-256
  `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`の1種類だけとする。
- SHA-256はファイル全体に対して計算する。長さまたはhashが一致しないファイルは`FIRMWARE_REJECTED`で拒否し、
  未知imageを強制使用するoptionは設けない。
- MLT5系にも同じ1種類のimageだけを受理する。`tsukumijima/px4_drv`はMLT5系とQ3U4に同一の
  `it930x-firmware.bin`を使用し、v0.16のDTV02A-5TS-P実機試験も同じimageで行う。
- 別imageを追加する場合は、実機で8 receiverとcard readerの受入試験を通し、本節へ長さ、SHA-256、由来を
  追記する仕様変更を先に行う。
- firmwareの探索、ダウンロード、archive展開、vendor driverからの抽出は行わない。
- git追跡対象、source archive、release artifactへfirmwareまたは元vendor driverを含めない。

### 4.6 Enclosure enumeration

`px4d --list`は、4.1節の通常列挙とgroupingで見つかった対象筐体をstdoutへ出力して終了する。
利用側が4.1節のUSB IDや識別子規則を持たずに、`px4d --device`へ渡す識別子と各receiverの方式を得るための口である。

- `--list`は単独でだけ受理し、他のoptionと組み合わせた場合はusage error（exit 2）とする。firmware、
  runtime directory、稼働中の`px4d`を必要としない。
- 列挙はdescriptorとserial stringの読み取りだけを行い、interfaceをclaimしない。firmware load、GPIO、
  LNB、カードには触れない。OSが所有中のデバイスのopenを許す環境では、別の`px4d`が所有中の筐体も
  列挙できる。
- 出力は1行1 recordの空白区切り`key=value`とし、筐体ごとに次の順で出す。
  - 筐体行: `serial=<識別子> model=<機種名> usb=<vid>:<pid> status=<状態> receivers=<数>`。
    `serial`は`--device`へ渡す4.1節の識別子、`model`は4.1節の表の機種名、`usb`は4桁小文字16進、
    `status`はgroupingの結果（`ready`、`incomplete`、`duplicate`、`invalid_observation`）とする。
    `ready`以外の筐体では`px4d --device`は起動できない。
  - 続く`receivers`個のreceiver行: `px4ctl list`（6.4節`LIST`）と同じ書式
    `receiver=<ID> device=<dev_id> local=<local_id> system=<ISDB-T|ISDB-S|ISDB-T/S>`で、4.2節の表をそのまま出す。
- 対象機種のUSB IDを持ちながら筐体にまとめられなかったUSBデバイスは、筐体行の後に
  `rejected serial=<serial> model=<機種名> usb=<vid>:<pid> status=<理由>`として1台1行で出す。理由は
  serialを読めない、または4.1節の形式に合わない`invalid_serial`、もしくはopenまたはdescriptor取得に
  失敗した`open_failed`（権限不足など。serialは空）とする。`open_failed`のデバイスを、serialが空であることから
  `invalid_serial`と報告しない。`serial`の印字可能ASCII（空白を除く）以外の文字は`?`に置き換える。
  rejected行どうしの順序は規定しない。対象機種以外のUSBデバイスは出力しない。
  速度やtopologyが条件を満たさないデバイスは筐体にまとめたうえで、その筐体を`invalid_observation`とする。
- 対象機種のUSBデバイスが1つもなければ何も出力せずexit 0とする。筐体がなくrejected行だけの場合も
  exit 0とする。列挙自体の失敗やstdoutへの書き込み失敗はstderrへ理由を出し、6.5節のexit codeで終了する。
- Android（Termux/APK）のように、USBデバイスを通常列挙・openできずfdを受け取って起動する環境では、
  `--list`の結果を保証しない。

v0.26では`--list`の既存行頭・既存項目・receiver行の並びを維持し、次の項目を末尾へ追加する。
既存の`serial`は観測値であり、それだけで`--device`が成功することを保証しない。

- 筐体行に`serial_unique=<true|false>`を加える。この値は列挙結果内でreadyかつ同じserialの群が
  1つだけで、serialによって筐体を一意に選択できることを示す。runtimeのBUSYやUSB権限などによる
  起動失敗は含めず、起動成功を保証しない。同じserialの群が状態を問わず複数ある場合、および
  ready以外の群はfalseとする。
  単一USB筐体とincomplete筐体も1行ずつ保持する。
  ready/incomplete/invalid_observation群の各観測USBには`dev1_bus`・`dev1_address`・`dev1_port`
  （2 bridgeでは`dev2_*`も）を加える。取得できない項目は省略する。duplicate群は全観測を失わず、
  `candidate1_device`・`candidate1_bus`・`candidate1_address`・`candidate1_port`から始まる
  `candidateN_*`を同じ筐体行に追加する。`device`はbridgeのdev_idとする。
- receiver行に`lnb_15v_supported=<true|false>`を加える。ISDB-Sを受信でき、かつ機種の
  `DeviceProfile.supports_lnb_15v`がtrueのreceiverだけtrueとする。ISDB-T専用receiverはfalse。
  この値はdaemonの`--allow-lnb-power`や実機検証状態を表さない。
- rejected行には観測できた`bus`・`address`・`port`を末尾へ加える。既存の`rejected`行頭と
  `invalid_serial`/`open_failed`の理由は維持する。bus/addressは10進、portは`BUS-PORT[.PORT...]`とする。

`px4d --list-json`は`--list`と排他的な独立オプションとし、他のオプションとの併用はusage error
（exit 2）とする。同じdescriptor読み取りだけを行い、1行の整形しない単一JSON文書を改行1つ付きで出す。
対象機器がなくても`{"enclosures":[],"ungrouped_usb_devices":[]}`を出す。列挙に失敗した場合は文書を
出力せず、stderrと非ゼロ終了で伝える。stdout書込みに失敗した場合は部分出力があり得るが、
stderrへ理由を出して非ゼロ終了する。

- トップレベルは`enclosures`と`ungrouped_usb_devices`の2配列を持つobjectとする。
- `enclosures[]`は`serial`、`model`、`usb`、`status`、`serial_unique`、`devices`、`candidates`、
  `receivers`を持つ。`serial`/`model`/`usb`/`status`はテキストの筐体行と同じ文字列、
  `serial_unique`はbooleanとする。`devices`は当該筐体に一意に割り当てた観測USBをdev_id順に載せる。
  duplicate群では`devices`を空にし、全観測を`candidates`へ載せる。その他の群では`candidates`を空にする。
- `devices[]`と`candidates[]`は`device`（1または2）、`serial`（USBの文字列）、`bus`、`address`、
  `port`を持つ。取得できない位置はJSON nullとする。未接続のbridgeは要素を作らない。
  `receivers[]`は4.2節の各receiverについて`receiver`、`device`、`local`（整数）、
  `system`（`ISDB-T`/`ISDB-S`/`ISDB-T/S`）、`lnb_15v_supported`（boolean）を持つ。
- `ungrouped_usb_devices[]`は筐体へ割り当てられない対象USBだけを載せ、`serial`、`model`、`usb`、
  `status`、`bus`、`address`、`port`を持つ。`status`は`invalid_serial`または`open_failed`、
  serialを読めないときはnull、位置が読めないときは各項目をnullとする。対象外USB IDは載せない。
- JSON文字列は制御文字を正しくエスケープする。`schema_version`は置かない。消費側は未知の追加キーを無視し、
  必須キーの削除・型変更・意味変更が必要なときは別のCLIオプションで新形式を定義する。
  `serial_unique=false`や`status=duplicate`の要素について、serialだけを選択子として保存してはならない。

## 5. IC card reader contract

### 5.1 Ownership and public interface

カードUARTとT=1 sessionは`px4d`が所有する。クライアントがT=1 sequence numberを保持しない。

portable IPCは以下を提供する。

- reader status
- card present/removed event
- ATR取得
- card reset
- APDU transmit
- transaction begin/end

複数clientからのAPDUは1つのカードsession上で直列化する。transaction中は他clientのAPDUを割り込ませない。
client切断、USB切断、カード抜去、timeout、protocol errorではtransactionとT=1 sessionを破棄する。

LinuxおよびmacOSではpcsc-lite/PCSC.framework向けIFD Handlerをportable IPCのadapterとして提供する。
初期Q3U4実機受入ではportable IPCと`px4ctl`による
ATR/APDU成功を必須とし、OS固有PC/SC adapterの完成は各OSのruntime supportを宣言する条件とする。
Androidではsystem PC/SCを前提とせず、portable IPCを利用する。

### 5.2 Hardware and power

以下のPX-Q3U4の2 bridgeに関する要件はsingle receiver機種には適用しない。

- backend power stateは筐体全体の単純な1カウンタではなく、`dev_id 1`と`dev_id 2`ごとに保持する。
- 既定modeは`all`とし、どちらかのIT9305Eでreceiverが1つでもopenなら両IT9305Eのbackend powerを維持する。
- card openは`dev_id 1`だけをpower userにする。receiverが1つもopenでなければ`dev_id 2`まで通電させない。
- card open時は`dev_id 1`のpower requestを記録してから`it930x_bcas_init()`相当を行い、初期化失敗時は
  そのrequestをrollbackする。
- 各bridgeの最後のreceiverを閉じても、反対側のreceiverとの連動または`dev_id 1`のcard requestがあれば、
  必要なbackend powerを停止しない。
- LNB powerはbridgeごとのGPIO 11と参照数で管理し、そのbridge上で15Vを要求する最後のISDB-S receiverを
  閉じたときだけ0Vへ戻す。
- receiverごとに直前に成功したtuneのLNB要求を保持する。retuneでは新しい0V/15V要求をtune前に適用し、
  後続のtune処理が失敗した場合は直前の要求へ戻す。参照の追加・削除とGPIO遷移はbridge単位で直列化する。
- GPIO 11への書込みが成功応答なしで終わった場合、物理状態を推測して`off`または`on`と確定しない。
  `DISCONNECTED`以外は`unknown`とcleanup debtを記録し、参照が0になったclose、shutdown、reconcileで
  GPIO 11 lowを再試行する。`DISCONNECTED`となったbridgeには以後の電源操作を送らない。
- 片側USB切断で筐体を停止する場合、切断したbridgeには追加書込みを行わない。接続が残る反対側bridgeは、
  そのtransportを閉じる前にLNB参照を解放してGPIO 11 lowを試みる。失敗は成功扱いにしない。
- MLT5系は単一bridgeのGPIO 7/2でbackend powerを、GPIO 11でLNBを制御する（Q3U4と同じGPIO割当と
  80ms/20ms順序）。いずれかのreceiverがopen、またはcard openならbackend powerを維持し、両方がなくなったときだけ
  停止する。LNBは「直前に成功したtuneが15VのISDB-S receiver」の参照数で管理し、ISDB-Tへのretuneとcloseで
  そのreceiverの参照を外す。`--allow-lnb-power`、GPIO応答喪失時の`unknown`とcleanup debt、`DISCONNECTED`後の
  書込み禁止はQ3U4と同じ規則とする。
- MLT5系の同一I2C bus上のtunerはdemodのI2C gate経由で同じaddress `0x60`を共有するため、gate open、tuner access、
  gate closeの一連をbus単位で直列化する。
- MLT5系ではreceiverごとにdemod/tunerをopen時に初期化し、close時に停止する。最初のstream開始前に
  IT930x PSBをpurgeする。
- MLT8PE3、MLT8PE5、DTV02A-4TS-Pのreceiver配線はupstream `driver/pxmlt_device.c`の
  `pxmlt_device_params[][]`に一致させる。wire tagはreceiver indexではなくTS port番号+1とする。
- M1UR/S1UR/ISDBT2071/ISDB2056/ISDB2056NはTC90522とR850/RT710を使うmodel-specific frontendを実装し、
  streamはtag demuxなしのsingle plain TSとして扱う。PX-M1UR/PX-S1URはcanonical Linux x86_64でprofile認定済み。
  その他の機種はprofile固有の実機初期化・tuning認定が完了するまでhardware-unverified。
- GPIO 11の初期化・確認は`DeviceProfile.supports_lnb_15v=true`の機種だけで行う。15V要求は
  daemonの明示的opt-inと当該profileのtrueの両方を必要とする。
  現在のsingle receiver全5機種は4.1節の`supports_lnb_15v=false`であり、参照`px4_drv`に合わせて
  初期化・受信・終了時にGPIO 11を設定・読取り・駆動しない。T/S兼用のPX-M1UR、DTV02-1T1S-U、DTV02A-1T1S-Uでは
  0V要求のISDB-S受信を許可し、15V要求は`--allow-lnb-power`の有無にかかわらず、GPIO書込み前に
  `UNSUPPORTED`で拒否する。T専用のPX-S1UR/DTV03A-1TUではISDB-S要求自体を拒否する。
  USB給電という事実だけを15V出力不能の根拠とはしない。DTV02系の実機電圧は未測定である。
- LNB 15V対応profileでは、通常の正常終了およびSIGINT、SIGTERM、SIGHUPの受信時、USB transportを閉じる前に
  各bridgeのLNBを0Vへ戻す。SIGKILL、host crash、USB stack failureではcleanupを保証できないため、
  明示的なopt-inと再初期化時のGPIO 11 lowを安全境界とする。現行の非対応5機種はこのGPIO cleanup規則の
  対象外で、安全境界は15V要求の無条件拒否とGPIO 11の不操作である。v0.1.7で15V要求を試した個体は、修正版で
  継続利用する前にUSBを物理的に抜き差しし、旧状態を持ち越さない。抜き差し前に給電状態の安全を推定しない。
- `px4-termux`のstage 0は、通常終了時およびSIGINT、SIGTERM、SIGHUPの受信時に、固定40秒の猶予を設けて子プロセスグループの終了を待つ。
  40秒後もグループが生存している場合のみSIGKILLを使用し、標準エラー出力へ警告を出力する。この強制終了経路では、graceful cleanup、
  LNB 0V、およびruntime endpoint削除を保証しない。

### 5.3 ATR and T=1

派生元のWinUSB実装にあった`SmartCard`状態機械を参照し、少なくとも以下を維持する。

- direct convention `0x3b`とinverse convention `0x3f`
- TA1 `0x11`、`0x12`、`0x13`
- TA3/IFSC、TB3/BWI、TC3/LRCまたはCRC
- TCK検証、ATR最大33 byte、余分なbyteの拒否
- UART frame上限255 byte
- LRC時INF上限251 byte、CRC時INF上限250 byte
- I-block chaining、R-block retry、S-block RESYNCH/IFS/WTX
- APDU全体の絶対期限3000ms
- block retry最大3回
- malformed ATRだけcard resetを1回再試行
- 失敗時にATR、IFSC、EDC、timeout、送受信sequenceを一括破棄
- 254-byte APDUをUART上限に合わせて分割

B-CASの実カードをQ3U4受入試験の対象とする。ACASは模擬試験を保持できるが、実機未確認なら対応済みと表現しない。

## 6. IPC contract

### 6.1 Transport and authorization

- transportは同一ホスト内のfilesystem Unix domain socketとし、TCP、abstract Unix socket、loopback networkへ
  fallbackしない。
- endpointは既定で`$XDG_RUNTIME_DIR/px4-userland/<instance>/`に作り、directoryを`0700`、socketを
  `0600`とする。明示したgroup共有modeだけdirectoryを`0750`、socketを`0660`にできる。
- control endpointとTS data endpointは分離し、control/card request、遅いTS client、別receiverのいずれも
  USB event loopまたは他receiverをblockしてはならない。

### 6.2 Common frame

全整数はlittle-endianとし、signed値は2の補数で表す。各frameは次の20-byte headerと
`payload_length` byteのpayloadを持つ。

| Offset | Type | Field | Contract |
|---:|---|---|---|
| 0 | `u8[4]` | magic | ASCII `PX4U` |
| 4 | `u16` | major | IPC protocol v1では`1` |
| 6 | `u16` | minor | IPC protocol v1では`0` |
| 8 | `u16` | type | 6.4節のmessage type |
| 10 | `u16` | flags | bit 0=response、bit 1=error、他は0 |
| 12 | `u32` | request_id | request/responseで同値、eventは0 |
| 16 | `u32` | payload_length | control最大65,536、TS_DATA最大1,048,596 |

- reserved flag、未知type、不正magic、上限超過、宣言長と実長の不一致はconnection単位の`PROTOCOL_ERROR`とし、
  そのconnectionを閉じる。allocation前に長さを検査する。
- pointer、`bool`、`size_t`、native enum、native struct padding、NUL終端前提の文字列をwireへ出さない。
- 文字列は`u16 byte_length`とその長さのUTF-8で表す。不正UTF-8と埋め込みNULを拒否する。
- responseはrequestと同じtype/request_idを持つ。error responseのpayloadは`u32 error_code`、
  `u16 detail_length`、UTF-8 detailとする。detailは診断用で、client分岐にはerror codeだけを使う。
- 1 control connectionで同時にoutstandingにできるrequestは1つとする。clientはconnectionを閉じてcancelできる。
  daemonは有限timeoutで処理を中止し、lease、transaction、card sessionを解放する。

### 6.3 Version, leases, and flow control

- 各connectionの最初のframeは`HELLO`または`ATTACH_STREAM`でなければならない。
- `HELLO` payloadは`u16 min_major, u16 min_minor, u16 max_major, u16 max_minor, u32 requested_capabilities`とする。
  互換majorがなければ`VERSION_MISMATCH`を返してcloseする。major 1ではserver minor以下の機能だけを使用する。
- `ACQUIRE`成功時、daemonは`u64 lease_id`と128-bit CSPRNG nonceを返す。leaseはcontrol connection、receiver、
  instanceに結び付け、再接続へ持ち越せない。
- `START_STREAM`成功後5秒以内にdata endpointへ接続し、`ATTACH_STREAM` payloadとしてlease IDとnonceを送る。
  tokenはarm単位で有効とする。armは`START_STREAM`成功から`ATTACH_STREAM`受付または5秒満了までを指し、
  各armは1回のattachmentだけを受け付ける。期限切れ、同一arm内での再利用、別instanceのtokenは拒否する。
- 同一leaseの再選局を許す。`STOP_STREAM`後のleaseは`consumed`となり、`TUNE`成功で再び`START_STREAM`を
  実行できる。nonceは`ACQUIRE`が発行した値をleaseの間使い回し、`START_STREAM`を実行するたびに新しいarmを
  開始する。wire形式は変わらず、`START_STREAM`応答はemptyのままとする。
- receiverごとのTS queueは有限長とし、既定65,536 packet、設定可能範囲4,096..262,144 packetとする。
  queue満杯ではUSB callbackをblockせず、そのclientを`SLOW_CONSUMER`で終了する。dropして配信継続しない。
- `TS_DATA` payloadは`u64 sequence, u64 cumulative_drop_count, u32 byte_count, bytes[byte_count]`とし、
  `byte_count`は188の倍数かつ1,048,576以下とする。正常streamではdrop countは常に0、sequenceはframeごとに1増える。

### 6.4 Message types and payloads

| Type | Name | Request payload | Success payload |
|---:|---|---|---|
| `0x0001` | `HELLO` | 6.3節 | `u16 major, u16 minor, u32 capabilities` |
| `0x0010` | `LIST` | empty | 4.1/4.2の固定情報 |
| `0x0011` | `STATUS` | empty | ready、USB/card presence、8 receiver state、error counters |
| `0x0020` | `ACQUIRE` | `u8 receiver_id` | `u64 lease_id, u8 nonce[16]` |
| `0x0021` | `RELEASE` | `u64 lease_id` | empty |
| `0x0022` | `TUNE` | `u64 lease_id, u8 system, u64 frequency_khz, u16 stream_id, u16 slot, u32 bandwidth_hz, u8 lnb_voltage, u32 timeout_ms` | lock stateとC/N |
| `0x0023` | `START_STREAM` | `u64 lease_id` | empty |
| `0x0024` | `STOP_STREAM` | `u64 lease_id` | final counters |
| `0x0025` | `STATS` | `u64 lease_id` | packet、byte、sync、TEI、continuity、queue、USB counters |
| `0x0030` | `CARD_STATUS` | empty | present、initialized、ATR、reader generation |
| `0x0031` | `CARD_CONNECT` | `u8 share_mode` | `u64 card_handle, u8 atr_length, atr[]` |
| `0x0032` | `CARD_RECONNECT` | `u64 card_handle, u8 share_mode, u8 disposition` | `u8 atr_length, atr[]` |
| `0x0033` | `CARD_DISCONNECT` | `u64 card_handle, u8 disposition` | empty |
| `0x0034` | `CARD_RESET` | `u64 card_handle` | ATR |
| `0x0035` | `CARD_TRANSMIT` | `u64 card_handle, u32 length, bytes[length]` | `u32 length, bytes[length]` |
| `0x0036` | `BEGIN_TRANSACTION` | `u64 card_handle` | empty |
| `0x0037` | `END_TRANSACTION` | `u64 card_handle, u8 disposition` | empty |
| `0x0040` | `ATTACH_STREAM` | `u64 lease_id, u8 nonce[16]` | empty、その後serverからTS_DATA |
| `0x8040` | `TS_DATA` | server event only | 6.3節 |
| `0x80f0` | `DEVICE_EVENT` | server event only | generation、event kind、affected receiver/card |
| `0x80ff` | `STREAM_END` | server event only | final countersと`u32 error_code` |

- `system`は1=`ISDB_T`、2=`ISDB_S`、`lnb_voltage`は0または15、未使用のstream ID/slotは`0xffff`とする。
- tune timeoutは100..30,000msとし、CLI既定値は10,000msとする。範囲外をclampせず`INVALID_ARGUMENT`で拒否する。
- CARD_TRANSMITのrequest/response上限は各4,096 byteとし、APDU全体の期限は5.3節の3,000msを超えない。
- event subscriptionはHELLO capabilitiesの`EVENTS` bitを要求したconnectionだけに行う。eventはresponseの途中へ
  byte単位で割り込まず、frame単位でのみ挿入できる。

capability bitはbit 0=`EVENTS`、bit 1=`CARD`、bit 2=`STREAM_STATS`とし、未知bitは無視する。
HELLO responseのcapabilitiesはrequestとserver対応bitの積集合とする。
可変配列は先頭に`u16 count`、可変byte列は先頭に`u32 length`を置く。ATRだけは上限33なので`u8 length`とする。
各recordは次の固定layoutとし、field追加または意味変更はprotocol minor更新を要する。

- `LIST` success: `u64 generation, u16 serial_length, serial_utf8, u8 ready, u8 usb_present_mask,
  u8 receiver_count, u8 card_reader_count`に続き、receiver count個の
  `u8 global_id, u8 dev_id, u8 local_id, u8 system`。v1ではcountは1、3、4、5、8、card reader countは1。
- `STATUS` success: `u64 generation, u8 ready, u8 usb_present_mask, u8 card_present,
  u8 card_initialized`、8個の`u8 receiver_state`、続いて`u64 usb_errors, u64 protocol_errors`。
  存在しないslotはstate 5=`absent`とする。
  receiver stateは0=free、1=leased、2=tuned、3=streaming、4=error。
- `TUNE` success: `u8 locked, i32 cnr_mdb`。C/N不明は`INT32_MIN`。
- `STATS`およびfinal counters: 順に`u64 packets, bytes, sync_errors, tei_packets,
  continuity_errors, queue_drops, usb_errors, empty_intervals`。
  `empty_intervals`はstream pumpがUSB完了を待った際のTIMEOUTまたは長さ0のcompletion回数であり、burst転送の
  正常動作中にも増加し得るbridge単位の診断値とする。同一bridgeのactive receiverへ同じ増分が反映されるため、
  receiver別starvationの判定には使用しない。非zeroであることだけをTS integrity failureにせず、受入ではpacket/byteの
  進行と、sync/TEI/continuity/queue/USB counterを別に判定する。この明確化はwire semanticsの変更ではなく、
  protocol minorを更新しない。
- `CARD_STATUS` success: `u8 present, u8 initialized, u64 reader_generation, u8 atr_length,
  atr[atr_length]`。未初期化時のATR lengthは0。
- `CARD_RESET` success: `u8 atr_length, atr[atr_length]`。
- `DEVICE_EVENT`: `u64 generation, u8 kind, u8 target_type, u8 target_id`。kindは1=attached、
  2=detached、3=card_inserted、4=card_removed、5=state_changed。target typeは1=device、2=receiver、3=card。
- `STREAM_END`: final countersの後に`u32 error_code`。

`LIST`/`STATUS`のreserved countやmask、ATR上限、receiver ID、enum範囲を受信側で検証する。上記layoutから
golden byte vectorを作り、異なる言語でencode/decodeしたvectorの一致をprotocol v1の受入条件とする。

v0.18では、layoutとfield数を変えずにreceiver countとrecord値域を追加する。Q3U4 daemonが送るbyte列は従来と完全に同一である。

- `LIST`の`receiver_count`は1、3、4、5または8とし、receiver recordはcount個だけ続く。
  count 8のrecordは従来のQ3U4固定表と一致しなければならない。single-USB flexible-system recordは
  `global_id == local_id == index`、`dev_id == 1`、`system == 3`とする。`system` 3は`ISDB_T_OR_S`を表し、
  `TUNE`の`system`としては無効のままとする。single-USB機種の`usb_present_mask`は`0x01`だけを使う。
- `STATUS`のreceiver stateは常に8個とし、存在しないreceiver slotには新しい値5=`absent`を入れる。
  `absent`は存在するreceiverには使わない。
- `ACQUIRE`および各lease操作は、存在しないreceiver IDを`INVALID_ARGUMENT`で拒否する。
- protocol minorは0のままとする。header versionの一致検査は厳密一致であり、minorを上げると同じ配布物内の
  CLI、PC/SC adapter、既存golden vectorとの互換を理由なく失うためである。旧decoderは新receiver countの`LIST`を
  malformedとして拒否し、未知の値域を黙って誤解釈しない（fail closed）。Q3U4に対する意味は変更しない。

card handleは作成したcontrol connectionに結び付ける。share modeは1=shared、2=exclusive、dispositionは
0=leave、1=resetとする。異常切断は全handleとtransactionを解放し、最後のhandle解放時だけcard hardwareをcloseする。
exclusive handleがある間は他のconnectを`BUSY`にし、transaction中は同じhandle以外のAPDUを`BUSY`にする。

### 6.5 Error and CLI contract

固定error codeは、0=`OK`、1=`INVALID_ARGUMENT`、2=`VERSION_MISMATCH`、3=`NOT_FOUND`、4=`BUSY`、
5=`NOT_READY`、6=`TIMEOUT`、7=`USB_IO`、8=`DISCONNECTED`、9=`PROTOCOL_ERROR`、10=`FIRMWARE_REJECTED`、
11=`UNSUPPORTED`、12=`NO_CARD`、13=`CARD_REMOVED`、14=`BUFFER_TOO_SMALL`、15=`SLOW_CONSUMER`、
255=`INTERNAL`とする。Linuxの負errnoを公開しない。

CLI exit codeは0=success、2=usage、3=not found/not ready、4=busy、5=timeout、6=IPC version/protocol、
7=USB/disconnect、8=TS integrity/backpressure、9=card/protocol、10=firmware、70=internalとする。
`px4-ts`は要求されたduration/packet countへの到達または明示的な正常stopだけを0とし、daemon切断、USB抜去、
queue overflow、sync/TEI/drop検出を0にしない。stdoutはTSだけ、全診断はstderrへ出す。

`px4-ts --channel CH`は、公開チャンネル表記から`system`と`frequency_khz`、ISDB-Sではslotを導出する。
受け付ける表記は次の形式だけとし、接頭辞`T`/`BS`/`CS`は大文字に限る。前後の空白、符号、余分な文字、
先頭ゼロは拒否する。

- `T<NN>`および`<NN>`: 地上波物理チャンネルNN（ちょうど2桁の10進数、13〜62）。`system=ISDB_T`、
  `frequency_khz=395142 + NN * 6000`。slotは持たず、`--slot`/`--stream-id`と併用できない。
- `BS<NN>`: BSトランスポンダNN（ちょうど2桁の10進数、奇数01〜23）。`system=ISDB_S`、
  `frequency_khz=1049480 + ((NN - 1) / 2) * 38360`。`--slot`または`--stream-id`が必須。
- `BS<NN>_<S>`: 上記BSにslot S（1〜2桁の10進数で先頭ゼロなし、0〜11）を付けたもの。slotはSに固定し、
  `--slot`/`--stream-id`と併用できない。
- `CS<N>`: CSトランスポンダN（1〜2桁の10進数で先頭ゼロなし、偶数2〜24）。`system=ISDB_S`、
  `frequency_khz=1613000 + ((N - 2) / 2) * 40000`。既定slotは0とし、`--slot`/`--stream-id`で
  上書きできる。

`--channel`は`--system`・`--frequency-khz`と同時指定できず、これら以外のオプションとは併用できる。
不正な表記や範囲外はusage error（exit 2）とし、stderrへ理由を1行で出す。受信機が方式に対応するかの
判定はdaemonの既存検査に委ね、CLIは機種表を持たない。この表記は以後互換性を保つ公開インターフェースとする。

## 7. Portability contract

### 7.1 Common requirements

- portable coreはC++17/C11、libusb-1.0、標準固定幅整数だけを前提とする。
- `_GNU_SOURCE`、glibc内部symbol、`eventfd`、`signalfd`、`epoll`、GNU `getopt_long`をportable coreで使用しない。
- Linux固有最適化は任意のplatform adapterとし、機能成立の条件にしない。
- wall clockをtimeoutへ使わず、単調時計を使う。
- path、IPC endpoint、USB serialを固定長bufferへ無検証で格納しない。

### 7.2 Android/Bionic

- `px4d`は`--fd FD`を繰り返し受け取り、Q3U4の2 USB deviceをwrapできる。
- `--fd`は筐体のUSB device数だけ指定する。Q3U4は2個、single-receiver機種（PX-M1UR / PX-S1UR）とMLT5系は1個とし、0個または3個以上は拒否する。
  wrapした全deviceが選択した1筐体に属さなければ（例: single-device機種に2個、Q3U4の片側だけに1個）失敗する。
  v0.15までの「`--fd`はちょうど2個」はQ3U4についての規則として維持される。
- `px4-termux`の`--usb-device`は1回（PX-M1UR / PX-S1UR / MLT5系）または2回（Q3U4）とする。1回の場合もstage 0の監督、signal転送、
  40秒猶予は2 FD経路と同じとし、stage 1が`exec px4d --fd FD1 ...`を実行する。
- fd modeでは`/dev/bus/usb`の列挙を要求しない。
- fdの所有権とclose責任を明記し、double-closeしない。
- Android NDK API 24でaarch64、armv7a、x86_64をクロスビルドし、3つのABIを同一の配布・監査対象とする。
- ELF interpreterはBionic linker、libusbはstatic link、host RPATH/RUNPATHは空とする。
- Termux試験では正式ランチャー `px4-termux` が `termux-usb` から開いた2つのfdを渡し、通常のCLI/IPC経路を使用する。
- `px4-termux --usb-device PATH --usb-device PATH --firmware PATH [--device BASE_SERIAL] [--instance TOKEN] [--runtime-dir PATH] [--group] [--allow-lnb-power]`
  を提供する。利用者は異なる2つのデバイスパスを明示し、自動選択は行わない。ランチャーの実行時依存はTermux付属のsh、
  Termux:APIの `termux-usb`、およびtermux-apiパッケージの依存関係として提供される `util-linux` の `setsid` のみとし、
  Python、補助デーモン、eval、一時状態ファイルを要求しない。
- stage 0の `px4-termux` は監督プロセスとして残り、`setsid termux-usb -e CALLBACK USB1` を子プロセスとして起動して
  `wait` する。起動時に `setsid` と `termux-usb` の存在を明示検査する。stage 1は受領した1つ目のFDを保持したまま
  `exec termux-usb -e CALLBACK USB2` を実行し、stage 2は2つ目のFDを受け取って `exec px4d --fd FD1 --fd FD2 ...` を実行する。
  渡される2つのFDは互いに異なり、かつオープン中でなければならない。通常の正常終了および第2open失敗の終了ステータス（status）は
  そのまま伝播する。stage 0は `SIGINT`、`SIGTERM`、`SIGHUP` を受信すると子プロセスグループ全体へシグナルを転送して
  回収（reap）し、固定40秒の猶予内にグループの消滅を確認してから終了する。40秒の猶予内に終了しない場合のみSIGKILLを使用し、
  標準エラー出力へ graceful cleanup、LNB 0V、およびruntime endpoint削除を保証できない旨を警告する。第2open失敗、シグナル受信、
  USB切断のいずれにおいても有限時間で終了する。ランチャーは利用者が指定したruntime rootを自動で再帰削除しない。
- Android ad-hoc APK（Android USB Host APIからfdをnative側へ渡す経路）の実機検証はdtv-android所管であり、
  本リポジトリのrelease artifact・release gate・claimに含めない。APKはrelease artifactへ含めない。

### 7.3 Linux/macOS native linkage

- Linux production executableはlibusb 1.0.30を静的包含したmusl完全静的ELFとし、`readelf -l`のPT_INTERPと
  `readelf -d`のDT_NEEDEDを持たないことを検証する。IFD Handlerだけはhost-loadable shared objectとし、
  glibc archiveはglibc 2.31 baseline、musl archiveはmatching musl ABIでビルドする。
  macOS production executableはlibusb 1.0.30を静的包含したMach-Oとし、`otool -L`でlibusb dylibを持たず、
  dependencyがmacOS system（`/usr/lib/`、`/System/Library/`）だけであることを検証して、各出力をrelease evidenceへ
  保存する。macOS IFD bundleはlibusbをlinkしない。staticになっていたbinaryはdynamic releaseとして出さない。
- libusb、pcsc-lite、その他のhost dependencyをstaticまたはbundleした場合は、Androidと同等のexact source、license、
  notice、build/relink obligationsへ切り替える。

### 7.4 macOS

- libusbで列挙・claimできることを前提とし、DriverKit/kextを要求しない。
- hardware未確認の場合はrelease metadataへ明記する。

### 7.5 Windows (Phase 1)

- 正規toolchainはllvm-mingwのUCRT x86_64とし、toolchain archiveとlibusb Windows binaryの
  version・SHA-256をbuild scriptへ固定する。compiler/dependency binaryをtracked fileとして同梱しない。
- portable coreはC++17、例外・RTTI不使用を維持する。Windows固有処理は`userland/src/windows/`の
  platform adapterへ隔離し、既存POSIX codeは`#else`側で挙動を変えない。
- endpointは`--runtime-dir`／`--instance`をサポートし、既定は`%LOCALAPPDATA%`配下とする。
  same-user private accessを必須とし、制限付きDACL（current user SID）と所有検証、reparse point検査、
  ファイル同一性検査を行う。安全でないfallbackを持たない。`shared_group`はWindowsでは明示的に
  unsupportedとしてargument validationで失敗させる。
- endpoint pathはWindows AF_UNIXのbyte長上限を検査し、Unicode pathを明示的に扱う。
  serial/instance排他（SerialEndpointLease相当）をWindowsにも実装し、same-serial safetyを落とさない。
- worker wakeはpoll可能なsocketで実装する。nonceはBCryptGenRandomを用いる。
- shutdownはSetConsoleCtrlHandlerとcooperative parent-stop契約を提供する。親プロセス（.NET/denpa等）は
  `px4d`の標準入力をcloseまたはEOFにして停止を要求でき、`px4d`は`--exit-on-stdin-eof` opt-in時に
  stdin EOFを受けて通常のcleanup（LNB 0V、endpoint削除）を実行してから終了する。Ctrl+C等のconsole
  control eventでもcleanup完了を待つhandshakeを行う。`TerminateProcess`がcleanupを行うと仮定しない。
  既定LNB 0V、明示opt-in、shutdown時cleanupを維持する。
- TS binary stdoutはbyteを保持し、stop/deadline/cancelに応答する。匿名pipeのconsumer停止でも
  恒久blockせず、broken pipeを正しく扱う。file sinkとpipe sinkの両方を扱う。
- IT930x 1ms pacingとSystemCardTimeのsleepはhigh-resolution waitable timerを用い、失敗を扱う。
  process-global timer tweakは行わない。
- カードaccessはlibusb経由のCARD_*操作に限定し、WinSCard DLLに依存しない。

## 8. Repository end state

最終ツリーには、少なくとも以下だけを残す。

- portable sourceとplatform adapter
- supported Q3U4/W3U4/MLT系device definition
- CLI source
- unit/integration/hardware test tools
- CMake、CI、license、README、仕様・検証記録

v0.5のscope cleanupでは旧Windows専用source、build、package、試験記録とfirmware extraction projectを削除した。
参照由来は9節に残し、削除したsourceはdirect sourceの`tsukumijima/px4_drv` snapshot commit
`9eedea8c502875a788697984b93b50032339b9aa`から復元できる。

以下はv0.5で削除済みであり、repository end stateに含めない。

- Linux kernel module、chardev、ioctl ABI、DKMS、Debian DKMS package
- tracked firmware、firmwareを含むpackage素材
- 4.1表にないdevice implementationとpackage definition
- 旧px4_drvの導入文書、kernel build文書、非対象機種の利用文書

## 9. Prior-art classification

| Candidate | Classification | Use |
|---|---|---|
| `tsukumijima/px4_drv` Linux kernel driver | adapt/reference | Q3U4初期化、tune、TS、電源、エラー契約 |
| 同WinUSB `DriverHost_PX4` | adapt/reference (primary) | 既存userland USB、複数receiver、device ownership |
| 同`SmartCard` / `WinSCard_PX4` | adapt/reference (primary) | ATR、T=1、APDU、共有、実機試験 |
| `makeding/linux-with-card-reader`等 | reference | Q3U4 card GPIO/UART、Linux実機知見 |
| `siano-userland` | adapt/reference | libusb-only CLI、musl/Bionic、Android fd、CI |

公開GitHubとローカル調査では、Q3U4の8チューナーと内蔵カードをlibusb-onlyで所有し、
Linux・Android・macOSを対象にする既存実装は確認できなかった。このため本体はbuildと分類する。

## 10. Acceptance criteria

### 10.1 Offline and CI

1. `cmake -S . -B build -G Ninja -DPX4_BUILD_TESTS=ON`が成功する。
2. `cmake --build build`がwarningをerrorとして扱う設定で成功する。
3. `ctest --test-dir build --output-on-failure`が成功する。
4. Ubuntu/glibc、Alpine/musl、macOSでbuildとoffline testsが成功する。
5. Android NDK API 24の3 ABI build/ELF/artifact audit、`px4-termux`の構文およびプロセス・FD引き継ぎ試験が成功する。
6. Android ELFにglibc/musl loader、shared libusb、host RPATH/RUNPATHが含まれない。
7. mock USBによるQ3U4 grouping、firmware framing、I2C、tune sequence、bridge別TS demux、hotplug試験が成功する。
   MLT系についても識別、warm初期化順序、CXD2856ER/CXD2858ER tune sequence、profile別tag demux、LIST/STATUS値域、
   1 fd起動の試験が成功する。v0.18の追加機種の実機受入は別途必要である。
8. `smart_card_state_test`相当のATR、T=1、timeout、retry、APDU分割、抜去、再挿入試験が成功する。
9. IPCの全messageについてgolden byte vector、malformed frame、version negotiation、権限、異常切断、
   slow-consumer/backpressure、CLI exit code試験が成功する。
10. stream counter試験で、正常TS中に`empty_intervals`だけが非zeroでも成功し、他のerror counterが0でも
    packet/byteの進行が5秒停止した場合は失敗する。
11. fuzzまたは境界値試験でUSB response lengthとIPC payload lengthの範囲外アクセスがない。
12. `git ls-files`にfirmware binary、vendor driver binary、kernel module、DKMS、4.1表以外のdevice packageが残らない。
13. 全派生source fileに`SPDX-License-Identifier: GPL-2.0-only`を付け、LICENSEがGPL-2.0を示す。
14. `px4d --list`の出力について、4.1表の筐体行とreceiver表、`incomplete`、`rejected`行と
    serialの置換、対象筐体なしの試験、およびopenできないデバイスを`open_failed`として報告する列挙の試験が
    成功する。

### 10.2 Q3U4 hardware acceptance on Linux

以下はQ3U4の機種profileおよび該当feature/pathをhardware-verifiedとするqualification基準である。Stable releaseごとに
全項目を再実行する要件ではない。結果は10.2.8のevidence単位で記録し、10.5の影響判定に従って継承または失効させる。
新releaseでは10.5のartifact別短時間実機matrixを必ず実施し、soakは10.5.2に従ってユーザーが決定する。

1. `px4ctl list`が2 USB deviceを1筐体へまとめ、8 receiverと1 card readerを報告する。
2. firmwareを両IT9305Eへloadし、再openとプロセス再起動後も初期化できる。
3. receiver 0..7を各々open、tune、30秒capture、stop、再openできる。
4. 両bridgeへそれぞれ`0x17`..`0x47`を注入するmockと実受信で、dev_id 1をreceiver 0..3、dev_id 2を
   receiver 4..7へ分配し、bridgeを跨いだ混入がない。
5. 8 receiverを同時に30分captureできる。
6. tune完了後の測定区間で各streamを1秒以下の間隔で観測し、任意の連続5秒窓の中でpacketとbyteの両方が
   増加する。counterは単調非減少で、常に`bytes == packets * 188`を満たす。receiver 0--6ではsync error、
   TEI、queue drop、USB errorが0である。receiver 7の既知burstは10.2.6aの参照比較を適用する。
   `empty_intervals`は6.4節の診断値として記録するが、非zeroだけでは失敗としない。
7. continuity errorはdiscontinuity indicatorとtune境界を除外して計数する。receiver 0--6では測定区間で0とし、
   receiver 7の既知burstは10.2.6aの参照比較を適用する。
8. 1 receiverの停止または再tuneが、他receiverのTSを停止・混入させない。
9. receiverを片側だけ、両側、cardだけ、receiver+cardの順にopen/closeし、5.2節のbridge別power stateを満たす。
10. `--allow-lnb-power`なしでは15V要求をGPIO書込みなしで拒否する。opt-in時はLNB 0V/15Vとbridgeごとの
    複数ISDB-S receiverの参照数が正しく、最後の利用者のcloseでだけ停止する。GPIO応答喪失、通常終了、
    SIGINT、SIGTERM、SIGHUP、片側USB切断のcleanup規則は5.2節を満たす。
11. card未挿入、挿入、ATR、reset、基本APDU、抜去、再挿入が成功する。
12. 外付け標準readerで同じB-CASを使ったAPDU responseと、Q3U4内蔵readerのresponseが一致する。
13. 8 receiverの同時capture中にcard APDUを反復し、APDU failureが0である。TS error/dropはreceiver 0--6で0とし、
    receiver 7の既知burstは10.2.6aの参照比較を適用する。
14. card利用中にtunerをすべて閉じてもcard通信を継続し、tuner利用中にcardを閉じてもTSを継続する。
15. idle、streaming、card transaction中のUSB切断が有限時間で失敗を返し、hangまたはuse-after-freeを起こさない。
16. 再接続後に旧lease、ATR、T=1 sequence、TS端数を再利用せず、再列挙・再初期化できる。
17. 壁設備と完全に分離した開放端で0V、15V、cleanup後0Vを測定し、GPIO 11の極性と切替を確認する。
    この無負荷試験をもってLNB切替をhardware-verifiedとするが、代表負荷時の給電能力は未確認として
    `LNB switching hardware-verified / loaded supply unverified`と記録する。これはStableのブロッカーではない。

### 10.2.6a receiver 7 reference comparison

receiver 7でTEIまたはcontinuity errorのburstが発生した場合、同一Q3U4個体、同一アンテナ・電源・firmware、
同一周波数、同一測定時間および同等のcapture条件で`tsukumijima/px4_drv`を実行し、同じcounterを取得する。
px4-userlandのburstが参照結果より悪化せず、追加のUSB error、queue drop、stream停止、crash、stale leaseを
生じない場合、そのburstは既知制限として記録し、Stable受入のブロッカーにしない。比較条件を再現できない場合は
判定保留とし、receiver 7を無条件に合格扱いしない。

`v0.1.0 Beta`では、receiver 0--6は2時間soakでerror 0だった。receiver 7はTEI `10974`、
`continuity_errors=453`を記録した。同一試験個体では、参照`tsukumijima/px4_drv`でも約11k TEIの署名が再現した。
この結果は10.2.6aの既知制限の初期証拠として扱う。`px4-ts`はTS integrity errorをCLI exit code 8で報告する。
receiver 7の比較証拠は、Q3U4のstream、demux、transport、concurrencyまたはpowerに影響する変更時に
freshでなければならない。これらに影響しない変更では既存比較を継承できる。release recordへbaselineと
継承または再取得の理由を記録する。

### 10.2.7 Additional Q3/MLT/1-receiver profile hardware acceptance

本節の共通認定は、追加されたQ3系、MLT系、1 receiverの各model/device profileに個別に適用する。各profileは
canonical Linux x86_64で一度認定する。認定では4.1の識別・grouping、profileに定義された全receiverとsystem capability、
firmware、tune/retune、capture、stop/reopen、status、全receiverの同時streamとdemux、適用されるcard/ATR/APDU、
power/LNB、終了cleanupを確認し、該当機能を含む組合せを30分以上連続して動作させる。profileにない機能は未搭載または
該当なしと記録し、検証済みとは扱わない。この共通認定だけで別OS/runtime/access pathをhardware-verifiedと主張しては
ならない。別pathに固有の性質はtargeted evidenceとして個別に認定する。

T/S兼用single receiverの3機種ではLNB 15V出力の0/15/0測定は適用しない。ただし15V要求の否定系は
省略せず、daemonの`--allow-lnb-power`なし・ありの双方で`UNSUPPORTED`を返し、全single receiver
機種の初期化から終了までGPIO 11を設定・読取り・駆動しないことをoffline testで確認する。T専用機種は
ISDB-S要求の拒否を確認する。ISDB-SのLNB 0V受信はT/S兼用機種のprofile認定対象である。DTV02系の
電圧・受信は未実測であり、参照`px4_drv`との一致だけでhardware-verifiedとしない。

以下の1--7はMLT family固有の追加確認であり、Q3系追加機種および1 receiver profileへ一律に要求する手順ではない。
MLT profileをhardware-verifiedとする場合、前段の共通認定に加えて次を満たす。

1. `px4ctl list`が1 USB deviceを1筐体として報告し、機種固有のreceiver数とsystem capability、card readerを示す。
2. firmwareをloadし、プロセス再起動後も再初期化できる。
3. 少なくとも1 receiverでISDB-Tのtune、capture、stop、再openができ、TSにsync/TEI/queue/USB errorがない。
4. 同じleaseでISDB-TとISDB-Sを切り替えるretuneができる。衛星アンテナがない環境では、ISDB-Sのtune要求が
   有限時間で`TIMEOUT`になり、その後同じleaseでISDB-Tへ戻れることを記録し、衛星captureは未検証と表示する。
5. 全receiverの同時captureで、各receiverのTSがtagどおりに分配され混入がない。
6. card未挿入/挿入、ATR、reset、基本APDUが成功する。
7. receiverを使わずにcardだけをopenしても通信でき、receiverとcardの開閉順にかかわらず5.2節の電源規則を満たす。

runtimeに固有のaccess pathは別のevidenceとしてtargeted確認する。例としてQ3U4のTermux 2-FDとsingle-device機種の
1-FD、plain TS、Android USB Host APKのFD handoff、PC/SC adapter、model固有のcardまたはpower経路を扱う。
canonical Linux認定だけからこれらのruntime/path claimを推論してはならない。

Q3U4の10.2各項は、新機種の追加によって弱めない。v0.18で追加した機種のうちcanonical profile認定未完了のものは
hardware-unverifiedであり、上記のprofileごとの認定を経るまでhardware-verifiedと表記しない。PX-M1URとPX-S1URは
canonical Linux x86_64でprofile認定済みだが、別runtime/access pathのclaimは個別evidenceを要する。1 receiver profileでは対象に存在しないISDB-S
またはcardの項目を除き、そのprofileのsingle TS同期、model-specific tune/capture、stop/reopen、適用される電源・card
経路を確認する。

### 10.2.8 Qualification evidence and inheritance

qualification evidenceは少なくとも次の独立軸を持つ。各レコードは、実行した組合せだけを立証する。

| Axis | Required value |
|---|---|
| Model/device profile | model名、USB ID、profile/version、device identity（必要な範囲で一貫して識別） |
| Runtime/access path | OSとversion、libc/runtime、native libusb、HAOS add-on、Termux 1-FD/2-FD、APK、PC/SC adapter等 |
| Feature/path | streaming、grouping、card/ATR/APDU、power/LNB、IPC/lease/retune、launcher/FD handoff等の対象と結果 |
| Artifact/source | exact artifact名とversion、archive SHA-256、source commit、関連build/toolchain識別子 |
| Test context | host/device identity、runtime version、実施日時、試験条件、結果、ログまたは保存先 |

未観測のmodel × runtime/access path × feature/pathの直積をhardware-verifiedと主張してはならない。別runtimeの同じmodel、
別modelの同じruntime、または未試験featureへの推論は認めない。同一model・同一runtime/access path・同一featureの
baseline evidenceについて、対象artifactのbytesが同一であるか、その証拠以後の変更が対象pathへ影響しないと10.5の表で
判定できる場合に限り、evidence inheritanceを認める。影響がunknownまたはambiguousなら継承せず、
対象pathのtargeted requalificationを行う。観測された失敗は原因を切り分けるまで、そのhost/device tupleに限定して扱う。

### 10.3 Cross-platform support claims

support表示は機能軸を混ぜず、10.2.8のmodel/device profile × runtime/access path × feature/pathごとの証拠を基礎にする。
OS単位でaggregate claimを表示する場合は、そのclaimの対象model集合、runtime/access path、feature/pathを明記する。
その集合に含めたmodel/path/featureのすべてに根拠を要するが、未所有またはhardware-unverifiedのmodelを集合へ含める
必要はなく、それらは集合外として扱う。集合外の機種へOS claimを推論してはならない。表示上は少なくとも次の4列を区別する。

- `build-tested`: buildとoffline testsだけが成功。
- `tuner-hardware-verified`: 実機でgrouping、firmware、ISDB-T/S capture、stop/reopen、disconnect/reconnectが成功。
- `card-core-hardware-verified`: portable IPCでATR、reset、反復APDU、抜去/再挿入が成功。
- `native-card-adapter-verified`: Linux/macOSでは実PC/SC consumerからresetと反復APDUが成功。
  Androidはこの列を`not applicable`とする。

特定のmodel/runtime/access pathを`runtime-supported`と表記するには、その組合せの上記該当列を満たし、そのprofileの
全receiver同時stream中にnative card adapter
（Androidはportable IPC client）から反復APDUを行って、Q3U4では10.2本文および10.2.6a、その他profileでは10.2または
10.2.7の該当TS受入基準を適用したTS受入条件とcard error条件を満たさなければならない。

`tuner-hardware-verified`には4.1のdevice contractに定めるUSB device数とinstance grouping、firmware load、profileが
対応する各systemのtune/capture、stop/reopen、USB disconnect/reconnectを要する。`card-core-hardware-verified`にはATR、reset、反復APDU、
card抜去/再挿入、USB disconnect/reconnectを要する。buildとoffline testだけの場合、および該当するmodel/runtime/path/
featureのhardware evidenceがない場合は`build-tested / hardware-unverified`と表記する。

以下は既存support matrixのruntime/access pathと確認対象である。表自体は実機試験結果を立証しない。実施済み試験だけを、
日時・artifact・結果とともに個別のevidence recordへ取り込み、未試験の組合せを埋めない。

| Runtime/access path | Scope to record in qualification evidence |
|---|---|
| Latitude 5300 / AnduinOS (x86_64) native | native libusb、T/S capture、8 receiver、内蔵card、実PC/SC consumer |
| Linux aarch64 / GitHub Actions | native build、offline test、archive audit。hardware-unverified |
| M720q / SCS native Debian 13/glibc + HAOS kernel | native libusb、T/S capture、8 receiver、内蔵card、PC/SC |
| M720q / HAOS Alpine/musl Supervisor add-on | add-on内musl/libusb、T/S capture、8 receiver、内蔵card、PC/SC |
| Latitude 5300 / Alpine Docker | auxiliary build/parser smoke。SCS、HAOS add-on、Latitude nativeの代替不可 |
| Pixel 9a / aarch64 / Termux | 正式launcher、2-FD、Bionic CLI、T/S、内蔵card、process/FD/endpoint cleanup |
| Google TV Streamer / armv7a / Termux | 正式launcher、2-FD、Bionic CLI、T/S、内蔵card、process/FD/endpoint cleanup |
| Bliss OS / x86_64 / Termux | 正式launcher、2-FD、Bionic CLI、T/S、内蔵card、process/FD/endpoint cleanup |
| Google TV Streamer / ad-hoc APK | 対象外。実機検証はdtv-android所管であり、本リポジトリのevidence recordへ取り込まない |
| M2 Mac mini / macOS | native libusb、grouping、T/S、内蔵card、実PC/SC consumer |
| Windows 11 x64 / native libusb | `windows-2022` CI build、Windows native offline test、PE/import/archive audit。実機は`hardware-unverified` |

TermuxとAPKは同一hardwareでも別access pathである。APK経路はdtv-android所管として本リポジトリのclaim対象外とする。
SCS native、HAOS Alpine add-on、native Linux、macOSもそれぞれ別runtime pathであり、証拠が直接存在するか継承条件を
満たす場合以外、相互に代替しない。
Linux aarch64はnative build、offline test、archive auditを満たしても`build-tested / hardware-unverified`を維持する。
証拠継承または他architecture/runtimeの実機結果だけでhardware-verifiedへ昇格させてはならない。

### 10.4 Release artifacts

Linux/Android/macOSのstable配布物のplatform/architectureは次の8つのbinary archiveとする。source archiveは全binaryに共通で1つ作成する。

| Artifact | Runtime contract |
|---|---|
| `px4-userland-<version>-linux-glibc-x86_64.tar.gz` | x86_64 Linux、glibc 2.31 IFD |
| `px4-userland-<version>-linux-musl-x86_64.tar.gz` | x86_64 Linux、musl IFD |
| `px4-userland-<version>-linux-glibc-aarch64.tar.gz` | aarch64 Linux、glibc 2.31 IFD、実機未検証 |
| `px4-userland-<version>-linux-musl-aarch64.tar.gz` | aarch64 Linux、musl IFD、実機未検証 |
| `px4-userland-<version>-darwin-arm64.tar.gz` | Apple Silicon macOS |
| `px4-userland-<version>-android-aarch64.tar.gz` | Android API 24+、Bionic aarch64、Termux用 |
| `px4-userland-<version>-android-armv7a.tar.gz` | Android API 24+、Bionic armv7a、Termux/Google TV用 |
| `px4-userland-<version>-android-x86_64.tar.gz` | Android API 24+、Bionic x86_64、Termux/Bliss OS用 |

Windows Phase 1はx86_64の`px4-userland-<version>-windows-x86_64.zip`を追加配布物とする。同一toolchainで
ビルドしたlibusb DLL、`px4d.exe`／`px4-ts.exe`／`px4ctl.exe`、LICENSE、第三者notice、exact source coverageを
含み、PE architecture、import DLL、build path leak、legacy artifactを監査し、deterministic zipと最終archiveからの
smokeを要する。Windows archiveのgateは、Windows IPC adapterとCLIの実装と10.3の`build-tested` evidenceに加え、
10.5の必須短時間実機matrixのWindows行（exact candidate archiveを使用）が完了するまで未完了とする。実機確認が
得られるまでは`hardware-unverified`と表示し、`build-tested`だけでは当該gateを完了としない。WinSCard互換DLLと
Microsoft PC/SC IFD登録はPhase 2以降であり本gateの対象外とする。既存8 archiveの判定条件と順序は変更しない。

各archiveは該当platformの`px4d`、`px4-ts`、`px4ctl`、利用可能なnative card adapter、GPL license、README、検証済み
libusb 1.0.30のexact `libusb/COPYING`を含む。license textは追跡対象のpackaging materialからofflineで包装し、hashを検査する。
Android archiveは既存の3つのELFに加え、libusbをリンクしないシェルランチャー `px4-termux` を含む。既存3 ELFの
inventory、LGPL、NDK、corresponding-source、relink監査は弱めない。ランチャーはELF/static-link inventoryの
対象外であることを監査上明示する。
Linuxの3実行ファイルはlibusb 1.0.30を静的包含したmusl完全静的ELFとし、IFDだけがarchive名のhost libcに依存する。
Android版とmacOS版のlibusbもstatic linkとし、macOSの3実行ファイルはmacOS system libraryとframework以外に
依存しない。firmware、APK、HAOS add-on、
mirakcはどのrelease artifactにも含めない。
source archiveは `__pycache__/`、`.pyc`、`.pyo`、`.pyd` などのPythonバイトコードを含めない。

全対応platform共通のbinary archive release gateは次の全項目を満たすまで未完成とする。

1. 各binary archiveにGPL license (`LICENSE`)、exact libusb LGPL license/COPYING、prominent plain-text notice、
   `THIRD_PARTY_NOTICES.md`、`README.md`を含める。Android noticeはstatic libusb 1.0.30とNDK runtimeも明示する。
2. 同じGitHub Release pageにcorresponding-source archiveをbinary archiveと同行させ、exact px4-userland source、
   binaryに使ったexact libusb source、各sourceの検証hash、build/relink instructionsを含める。GitHub自動source
   archiveだけでは、downloaded libusb sourceがないためこの要件を満たさない。
3. Android static libusbはLGPL-2.1-or-laterのままとし、LGPL-2.1 §6(d) routeで、同じ場所から§6(a)のsource-and-relink
   materialsへアクセスできるようにする。完全なGPL-2.0-only px4-userland sourceから再ビルドできるため、application
   `.o`をrelinkable deliverableとして別途必須とはしない。
   libusbをLGPL §3によりGPL化したとは主張しない。
4. NDK r27の`libc++_static`/`libc++abi`について、Apache-2.0 WITH LLVM-exceptionのterms、`NOTICE`、
   `NOTICE.toolchain`をreleaseへ含め、実際にlinkされたarchive member inventoryをartifactごとに保存する。
5. Linux production executableは`readelf -l`でPT_INTERPなし、`readelf -d`でDT_NEEDEDなしを検証する。Linux IFDは
   archive名に対応するglibc 2.31またはmuslのshared objectとして監査し、macOS binaryは`otool -L`でlibusb dylibと
   macOS system以外のdependencyがないことを検証する。
6. Linux/macOS static libusbのexact source、license text、notice、build/relink instructionsは対応source archiveへ含める。
   Linux/macOS binary archiveの`DEPENDENCY-NOTICE.txt`はstatic libusb 1.0.30、LGPL-2.1-or-later、対応source archive名を
   明示し、全platformのbinary archiveで`libusb/COPYING`が検証済みexact license textと一致することを監査する。

このgateの包装・manifest・checksum・binary/source archive auditは、local packaging scriptsと
`.github/workflows/build_userland.yml`の`release-candidate` workflowとして実装済みである。workflowはtagや
GitHub Releaseを作成せず、9つのbinary archive（8つのtar archiveとWindows ZIP 1つ）、対応source archive、外側
`SHA256SUMS`をcandidate artifactとしてまとめる。release recordは各hardware claimについて、このcandidateのexact artifactで試験したか、従前artifactの
evidenceを継承したかを明記する。exact-current-artifactで未試験なら未試験と記録し、過去artifactの結果を今回の直接
試験として扱わない。Androidの各ABI/access pathは正式launcherを用いた独立したevidenceを要する。Linux aarch64は
archive auditとnative CI buildを必須とするが、実機未検証を既知の非ブロッカーとして公開時に明記する。

### 10.5 Stable release gate

v0.1.10に限る受入判断（2026-10-08、ユーザー決定）: 必須8 artifact matrixを全件実施したうえで、
receiver 7のTEI/continuity burstを既知不具合として継続し、ブロッカーから除く。
10.2.6aのfresh参照比較は実施結果を残すが、全試行の非悪化を証明したと表示しない。
macOSのUSB再接続後にreceiver 0〜3で観測したCC `5/5/6/6` は原因未解明の単発事象として残し、
追加の固定ABBA×5（USB再接続10回、USB保持・daemon再起動10回）でreceiver 0〜6の異常が
各条件0/10だった結果を基に非blockingとして受け入れる。追加試験は直接周波数指定であり、
元試行の混在した`--channel`指定との差、および低頻度・不存在を証明していない点を明記する。
これは当該releaseのリスク受入であり、原失敗を合格へ書き換えたり、hardware claimの範囲を広げたり、
今後のreleaseで同様の異常を自動免除する規則ではない。詳細は
[`validation-results.md`](docs/platforms/validation-results.md)のv0.1.10記録を参照する。

Stable公開前に、次の条件をすべて満たすこと。

1. 10.1のCI、静的監査、archive manifest、checksum、licenseおよびcorresponding-source監査が成功している。
2. 最終candidateが使用した9 binary archive（8つのtar archiveとWindows ZIP 1つ）、corresponding-source archive、
   outer checksumについて、10.4の全
   auditを完了し、再現性確認を成功させる。再現性確認は、final candidateと同一source commitに対する独立した
   2回のclean CI candidate run（別run・新規workspace）でrelease-candidate workflowを実行し、9 binary
   archiveとcorresponding-source archiveの10 archiveすべて（Windows ZIPを含む）のSHA-256が両runで一致し、
   各archive本体がbyte-identicalであり、かつ
   外側`SHA256SUMS`自体もbyte-identical（`cmp`）であることをもって成功とする。入力の一致は、pinnedされた
   分は同一revision/digest、workflow上floatする分は両runの実効toolchain/build inputの観測値が一致する
   ことで判定する。両runのrunner image version/ID、compiler/SDK/NDK等のartifact生成に用いるtoolchain
   識別子とbuild inputをrelease recordに残し、runner metadata（label/OS image）とartifact生成toolを区別
   する。実効toolchain/build inputが異なる場合、または記録から同一と確認できない場合は、その比較は
   inconclusiveであり、成功でも失敗でもない。原因を切り分け、同一inputのmatching pairを取り直して比較する
   まで成功としない。runner image versionが両runで異なっても、artifact生成に用いるtoolchain/build inputが
   同一であることをrelease record上で確認できれば一致として扱う。正規化（UUID・署名・timestampのマスク等）
   による合格は認めない。source commit、toolchain/build inputs、static/dynamic link inventory、relink結果、
   artifact checksum、licenseおよびsource提供条件をrelease recordに残す。
3. 公開するfinal candidateの配布binary archive 9種すべて（8つのtar archiveとWindows ZIP 1つ）について、対応する
   OS/architectureの実機で短時間確認を毎回行う。
   artifact別のhostと手順は`docs/release-validation.md`のmatrixに従い、各archive個別に列挙、daemon動作中の
   B-CASカード抜去/再挿入（不在状態・ATR/reset・反復APDUの復帰）、短い受信、停止、USB disconnect/reconnect後の復旧、
   process/endpoint残留確認を行う。Windows ZIPはWindows 11 x64の実機で、列挙・ready、receiver capture、CARD_*操作、
   USB再接続後のdaemon再起動、cooperative shutdown残留確認を行う。必須確認の一連の所要時間に上限は設けず、
   archive間で証拠を代用しない。
   5分を超える連続負荷試験は本項の短時間確認に含めず、soakとして扱う。必須の実機環境が利用できない場合は当該artifact gate未完了とする。
   別OS/ABIで代用したり、agent判断で免除したりしない。Linux musl aarch64とWindows 11 x64も含めmatrixの全配布targetに適用する。
4. B-CASカード抜去/再挿入と物理USB detach/reconnectは人手で実施する。各物理操作を要求する直前にHAOS側でCodexは`beep`、
   Claude Codeは`vibe`を実行し、操作要求から完了まで最大5分待つ。この5分は各物理操作への応答待ち上限であり、必須確認
   一連の所要時間は制限しない。5分以内に操作が行われない場合、その操作および当該artifact gateは未完了とする。
   カード抜去中は`NO_CARD`（exit 9）、挿入後はATR取得・reset・反復APDUの成功を確認する。
   USB再列挙・復旧も確認する。新しいhardware claimに必要な抜差しはSPEC 10.3に従い別途実施する。
5. release recordは各claimについて10.2.8のbaseline evidence、baseline以後の累積変更、impact判定、evidenceの継承または
   失効理由、今回のfresh testを記録する。exact-current-artifactで未試験のclaimは、その事実を明記する。未観測の
   model × runtime/access path × feature/pathをhardware-verifiedと表示しない。
6. 安定性に影響し得る変更がある場合、agentは差分、影響経路、対象となり得るOS/artifact/profile、過去記録と今回の
   短時間結果をまとめてユーザーへ提示する。soakを行うか、10分/30分/2時間のどれにするか、対象OSは何かをユーザーが
   決める。agentは選択を代行せず、ユーザー決定までsoakを開始しない。soakをしない選択も記録する。手順は
   `docs/release-validation.md`に定める。receiver 7のfresh比較条件は10.2.6aに従う。
7. crash、hang、use-after-free、stale lease、再接続不能、カード経路の重大な未解決issueがない。receiver 7の参照一致
   burstおよび代表負荷未検証のLNBは、この項の重大な未解決issueには含めない。
8. 公開前レビューを実施し、README、LICENSE、THIRD_PARTY_NOTICES、provenance、checksum、support表示および
   release archiveの内容が一致している。
9. Linux aarch64のglibc・musl各配布binaryは、native CI build、offline test、musl/ELF/IFD/archive監査に加え、
   `docs/release-validation.md`の必須短時間matrixをそれぞれ実機で通過する。実機hostがない、または未通過なら該当artifact gateは未完了であり、
   他のlibcやarchitectureの結果で代用しない。
10. 手持ちにない機種は、READMEの対応機種一覧へ検証状況を明記した上でリリースできる。実機を試したテスタが現れたら、負担にならない範囲の検証（`scripts/w3u4-report.sh`相当の実機報告）を依頼し、その報告をもって`hardware-verified`へ更新する。報告がない間は検証済みと表現しない。
11. Beta公開は対応機種の追加には使わない。Betaは機能追加や挙動変更など不安定な変更に限定し、機種追加は第10項の`hardware-unverified`表示を伴う通常リリースとして扱う。

#### 10.5.1 Change impact and evidence invalidation

下表はevidence inheritanceの最低限の分類である。「対象path」は該当変更が作用するすべてのmodel/runtime/access
path/featureを含む。表の列挙変更だけでなく、間接依存する変更も影響対象に含める。

| Change class | Evidence invalidated / required fresh qualification |
|---|---|
| USB transport、libusb version、static/dynamic link、link options | 変更したbinary/runtimeのUSB列挙、grouping、firmware、stream、hotplug、card等のUSB依存path。transportの共有層なら全model/profileで影響を評価する。 |
| compiler、toolchain、ABI、build flags | 再buildされた対象artifactとABI/runtime path。portable coreまたはABI境界を含む場合は依存featureをtargeted再認定する。 |
| platform adapter、IFD/PCSC、Android launcher、FD handoff | 該当platform/access pathのadapter、card、launcher/FD機能。Termux 1-FD/2-FD、APK、PC/SCはそれぞれ独立判定する。 |
| firmware accepted hash/format、firmware initialization | 対象profileの初期化、tune、stream、card/power初期化に依存するpath。 |
| stream、queue、demux、concurrency、lifetime、hotplug | 対象profileの全stream topology、stop/reopen、status、cardとの並行動作、detach/reconnect。Q3U4共通影響ではreceiver 7比較もfreshにする。 |
| card、ATR、T=1、APDU | card搭載profileのcard core、runtime adapter/IPC、tunerとの並行動作。 |
| power、LNB、GPIO、cleanup | 対象profileのpower state、receiver/card開閉順、LNB、正常・異常終了cleanup。 |
| IPC、wire format、lease、retune | IPC client/server双方の対象runtime、status、lease、same-lease retune、stream start/stop。 |
| device identity/profile/frontend | 変更対象model/profileの識別、grouping、receiver mapping、frontend tune、demux、power/card該当path。別profileへ自動継承しない。 |
| docs、license text、package metadata only | hardware evidenceを失効させない。full release auditとchecksum/license/source gateは毎回必要。 |

分類に当てはまらない変更、依存範囲が不明な変更、複数分類にまたがる変更はunknown/ambiguous impactとし、継承せず、
影響し得る最小のpath集合をtargeted requalificationする。impact判定をrelease recordへ記録する。

#### 10.5.2 Soak: user decision

Soakは5分を超える連続負荷試験であり、毎回のartifact別短時間実機確認とは別の手順である。変更内容から安定性への影響が
あり得る場合、agentは10.5.1の分類を使ってコード差分、call path/platform guard、影響し得るmodel/profile/runtime/
access path、過去の実機証拠、今回の短時間matrix結果と未確認点を説明する。これらはユーザーの判断材料であり、
soakを自動開始するtriggerではない。

ユーザーだけが次を決定する: soakを行うか（行わない選択を含む）、時間を10分/30分/2時間のどれにするか、対象OSを何に
するか。agentは時間・OSを独自に選択、既定、提案採用済み扱いしてはならず、明示的なユーザー決定があるまでsoakを
開始しない。ユーザーが候補比較を求めた場合は影響範囲と各選択が確認する内容を提示し、決定を待つ。releaseあたりの
最大OS数やcanonical順での自動選定は設けない。

ユーザーが実施を選んだ場合は、指定OS上で指定時間、対象profile/topologyに適用される10.2の受入項目を実行する。
Q3U4を指定された場合は8 receiverのISDB-T/S混在、必要なstatus/APDU/retune/stop-reopen、TS/counter、終了cleanupを
確認する。30分・2時間試験のFD数/RSSは傾向を記録し、固定数値閾値では判定しない。最終観測まで安定しない持続増加が
外部要因で説明できない場合は`判定保留`とし、passにしない。profile認定、serial衝突、receiver 7比較等の個別受入条件は
10.2.6a/10.2.7のまま維持し、毎回必須の短時間artifact matrixの代わりにはしない。

## 11. Implementation increments

1. Portable build、error、logging、mock transport、CIを作る。
2. libusb transport、通常列挙、Android repeated-fd wrap、Q3U4 groupingを作る。
3. firmware providerとIT930x control/I2Cを移植し、実機で初期化する。
4. TC90522/R850/RT710、1 receiver tune、TS captureを移植する。
5. 1 IT9305E上の4 receiver同時処理、TS aggregation/demuxを完成する。
6. 2 IT9305E、8 receiver、bridge間連動電源を完成する。
7. card hardware transportとportable ATR/T=1 state machineを完成する。
8. local IPC、`px4d`、`px4-ts`、`px4ctl`を完成する。
9. Linux/macOSのPC/SC adapterを接続し、確認できたOSだけsupport表記を更新する。
10. portable replacementごとにlegacy codeを削除し、READMEとrelease metadataをQ3U4-onlyへ更新する。
11. Windows Phase 1: llvm-mingw/UCRT x64でplatform adapter（socket、endpoint権限、非同期wake、nonce、
    sleep、console/signal、stdout）とWindows固有testを実装し、`px4d`／`px4-ts`／`px4ctl`とlocal IPCを
    クロスビルドする。winSCard DLLと実機hardware claimは含めない。各stepはoffline testまたは
    cross-buildがgreenになるまで次へ進めない。

各incrementは対応するoffline testまたは実機観測がgreenになるまで次へ進めない。

## 12. Constraints and rollback

- firmware、USB dump、カード情報、秘密情報をcommitしない。
- 実機試験は既存PX-S1UD/mirakc/EPGStationの稼働経路を変更しない隔離コマンドで行う。
- HA Core、アドオン、mirakcを自動で再起動しない。
- 既存テストを変更して失敗を隠さない。

実装失敗時は、作業branchを破棄せず失敗証拠を保存し、直前のgreen commitへ戻す。
削除したlegacy sourceは、direct sourceの`tsukumijima/px4_drv` snapshot commit
`9eedea8c502875a788697984b93b50032339b9aa`から復元できる。
