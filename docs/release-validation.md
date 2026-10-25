# 安定版リリース前検証手順

この文書はPX4のStable releaseで再利用する準備・操作・判定・cleanupの正本です。版ごとの適用matrixと検証記録は [`release-validation/1.0.0.md`](release-validation/1.0.0.md) のように版別文書へ記録します。過去候補の実測値は今回候補の合格を意味しません。CLI引数は現行 [`README.md`](../README.md) に合わせます。

この文書は Stable リリースごとの実行手順を定める。合否条件と認定条件の正本は [`SPEC.md`](../SPEC.md) 10章であり、両者に差があれば SPEC を優先し、手順書を更新する。

## 共通の検証項目と頻度

用語は4製品で共通とする。代表機はLinux x86_64 glibc、機種はチューナー、製品はドライバ、ソークは一定時間以上の連続したTS取得を指す。時間条件は下の適用頻度と版別matrixに従う。製品・機種ごとの能力、対応機能、実コマンドは続く製品固有節と手順節に記し、版別記録にはcandidateごとの実施結果を記す。

| 区分 | 適用頻度 | 共通の確認 |
|---|---|---|
| 機種初回 | 各製品の全対象機種で、機種ごとに代表機の2時間連続受信と毎release項目を実施する。必要時項目も通し、結果は機種別に初回記録へ残す。 | 当該機種の公開能力・対応機能に沿って実行し、別機種の結果で代替しない。 |
| 回帰（毎release） | 代表機で2時間連続受信する。全配布archiveをそれぞれ対応するnative OS/ABIで短時間実機回帰し、`--list-json`、機種の公開同時受信能力、TS/counter/終了値を確認する。最大同時受信で地上波・衛星の対象3波を確認できない場合は不足波を短時間取得する。受信中の状態と反復制御、正常停止、process/FD/IPC残留なし、対応機能のCARD挿抜、USB抜去/再挿入を確認する。native PC/SC/WinSCard経路がある製品・OSでは製品CLI経路と分けて確認する。 | 各archive/OS/ABIのrunと各機種の結果を分ける。未対応機能は理由付きでN/Aとし、未実施や失敗を合格に読み替えない。 |
| 必要に応じて | 対応機能ではLNB出力電圧を実測し、非対応機種の拒否を確認する。複数チューナーの列挙/daemon並列と、変更機能に応じた機種×OS/ABIの追加確認も行う。 | 適用する機種・機能・OS/ABIと実測値を記録する。 |
| 初回/major | 必要時項目を通し、代表機で全対象の無料channelを各10秒TS取得する。ソークはmacOS/Windows各30分、残るOS/ABI各10分とする。全channel取得は代表機だけで行い、別途指示されたソークは別に記録する。 | 30分/10分は各OS/ABIの個別時間であり、共通の一般定義である「一定時間以上の連続TS取得」を置き換えない。 |

全配布archiveのnative短時間回帰と代表機だけの全channel取得は別の確認である。候補の操作・期待値・cleanupは各archiveの具体手順に従い、実施状態・結果・未達は版別記録に残す。

## 製品固有の適用条件

PX4の通常の無指定時検証機種はPX-Q3U4だが、機種初回の対象を限定しない。PX4が対象とする全機種で初回手順を個別に実施し、結果は[`release-validation/initial-model-checks.md`](release-validation/initial-model-checks.md)へ機種別に、版ごとの実績は[`release-validation/1.0.0.md`](release-validation/1.0.0.md)へ記録する。

| 区分 | 製品固有の確認 |
|---|---|
| 機種初回 | Q3U4では8 receiverの地上/衛星同時受信、CARD、USB、対応profileのLNB、WinSCard/PCSCを確認する。ほかの機種はその機種の能力・対応機能に応じて確認する。 |
| 回帰（毎release） | Q3U4の8 receiver配置、B-CAS removed/ATR/APDU復帰、切断exit/daemon cleanupを以下の確立済み手順で確認。 |
| 必要に応じて | PX-M1UR/PX-S1URの非対応LNB拒否、同一serial曖昧指定、同一lease retuneや`--control`等の条件付き項目を該当変更時に行う。 |
| 初回/major | 現地channel planの地上/無料衛星対象channelを使い、地域/TSID/slot実値を記録する。 |

## 共通実行環境表

### 環境表データ辞書

| 列 | 意味・含める属性 | 含めない属性 | 表記・許容値 |
|---|---|---|---|
| ID | OS/ABIと必要なruntime/access境界を持つ実行環境シナリオの識別子 | candidate状態、過去実績 | `E` + 番号。4製品で共通するシナリオは同じID、別runtime/access scenarioは別ID |
| OS / ABI | OS系統、CPU ABI、libcまたはOS runtime | host名、依存、実施状態 | `OS / CPU ABI / libc・runtime` の順。根拠文書にlibc記載がない場合は `未特定` |
| 依存 / driver / permission | 実行時USB library、OS側USB access手段と権限条件 | firmware、CARD API、検証結果 | `libusb:`、`USB access:`、`FD handoff:` の属性名を使う |
| firmware / 補助data | 機器へ渡す実行時firmwareと、RF channelとは別の機種固有データ | RF試験channel、archiveへの同梱・埋込完了、candidate実績 | `firmware:` と `補助data:` を分け、ない属性は `なし` |
| RF試験入力（system / channel） | 実際に受信させる放送systemと現地channel | firmware、機種data、candidate実績 | `system:` と `channel:`。対応波がない場合は `なし（理由）` |
| 製品CLIからの内蔵CARD操作 | 製品固有CLI/APIの操作範囲 | PC/SC/WinSCard adapter、実機合否 | 操作名とmodel依存条件。機能なしは `なし` |
| native PC/SC/WinSCard adapter | その行のOS標準APIへ接続する製品adapterの種類・配布形態 | 製品CLI CARD、adapter実機試験結果、他OSのadapter | OS/ABI行に該当する`Linux PC/SC:`、`macOS PC/SC:`、`Android:`または`Windows WinSCard:`だけを記載 |
| 配布artifact / 手順 | 対象archive識別子とこの正本内の該当手順 | artifact hash、host、試験結果 | `artifact: <target>; procedure: [archive別手順](#3-配布アーカイブ別手順)` の順 |

共通IDは E02=Linux/x86_64/musl、E03=Linux/x86_64/glibc、E04=macOS/arm64、E05=Android/aarch64、E06=Android/armv7a、E07=Android/x86_64、E15-G=Linux/aarch64/glibc、E15-M=Linux/aarch64/musl、E17=Windows 11/x86_64を表す。IDはhost名や試験済み状態を表さない。

| ID | OS / ABI | 依存 / driver / permission | firmware / 補助data | RF試験入力（system / channel） | 製品CLIからの内蔵CARD操作 | native PC/SC/WinSCard adapter | 配布artifact / 手順 |
|---|---|---|---|---|---|---|---|
| E03 | Linux / x86_64 / glibc | libusb: 静的1.0.30; USB access: native OSの許可済み経路 | firmware: model firmware image; 補助data: なし | system: 対応modelのISDB-T/ISDB-S; channel: 現地channel planから対応channelを選択 | CARD status/ATR/reset/APDU CLI; 操作対応はprofile依存 | Linux PC/SC: libc対応IFD | artifact: `linux-glibc-x86_64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E02 | Linux / x86_64 / musl | libusb: 静的1.0.30; USB access: HAOS Supervisorの許可済み経路 | firmware: model firmware image; 補助data: なし | system: 対応modelのISDB-T/ISDB-S; channel: 現地channel planから対応channelを選択 | CARD status/ATR/reset/APDU CLI; 操作対応はprofile依存 | Linux PC/SC: libc対応IFD | artifact: `linux-musl-x86_64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E15-G | Linux / aarch64 / glibc | libusb: 静的1.0.30; USB access: native OSの許可済み経路 | firmware: model firmware image; 補助data: なし | system: 対応modelのISDB-T/ISDB-S; channel: 現地channel planから対応channelを選択 | CARD status/ATR/reset/APDU CLI; 操作対応はprofile依存 | Linux PC/SC: libc対応IFD | artifact: `linux-glibc-aarch64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E15-M | Linux / aarch64 / musl | libusb: 静的1.0.30; USB access: native OSの許可済み経路 | firmware: model firmware image; 補助data: なし | system: 対応modelのISDB-T/ISDB-S; channel: 現地channel planから対応channelを選択 | CARD status/ATR/reset/APDU CLI; 操作対応はprofile依存 | Linux PC/SC: libc対応IFD | artifact: `linux-musl-aarch64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E04 | macOS / arm64 / Apple system runtime | libusb: 静的1.0.30; USB access: macOSの許可済み経路 | firmware: model firmware image; 補助data: なし | system: 対応modelのISDB-T/ISDB-S; channel: 現地channel planから対応channelを選択 | CARD status/ATR/reset/APDU CLI; 操作対応はprofile依存 | macOS PC/SC: bundle | artifact: `darwin-arm64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E05 | Android (Termux) / aarch64 / Android NDK ABI | libusb: 静的1.0.30; USB access: Termux USB permission; FD handoff: daemonへ2 FD | firmware: model firmware image; 補助data: なし | system: 対応modelのISDB-T/ISDB-S; channel: 現地channel planから対応channelを選択 | CARD status/ATR/reset/APDU CLI; 操作対応はprofile依存 | Android: なし | artifact: `android-aarch64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E06 | Android (Termux) / armv7a / Android NDK ABI | libusb: 静的1.0.30; USB access: Termux USB permission; FD handoff: daemonへ2 FD | firmware: model firmware image; 補助data: なし | system: 対応modelのISDB-T/ISDB-S; channel: 現地channel planから対応channelを選択 | CARD status/ATR/reset/APDU CLI; 操作対応はprofile依存 | Android: なし | artifact: `android-armv7a`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E07 | Android (Termux) / x86_64 / Android NDK ABI | libusb: 静的1.0.30; USB access: Termux USB permission; FD handoff: daemonへ2 FD | firmware: model firmware image; 補助data: なし | system: 対応modelのISDB-T/ISDB-S; channel: 現地channel planから対応channelを選択 | CARD status/ATR/reset/APDU CLI; 操作対応はprofile依存 | Android: なし | artifact: `android-x86_64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E17 | Windows 11 / x86_64 / Win32 | libusb: 静的1.0.30; USB access: 既存許可済みWinUSB driver | firmware: model firmware image; 補助data: なし | system: 対応modelのISDB-T/ISDB-S; channel: 現地channel planから対応channelを選択 | CARD status/ATR/reset/APDU CLI; 操作対応はprofile依存 | Windows WinSCard: application-local限定DLL | artifact: `windows-x86_64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |

この表は現行配布targetとnative実行条件の対応である。実際のhost OS/ABI、USB access path、実施結果は各version文書へ記録する。production HAOS上で試験環境を変更する場合は別途承認済み手順に従う。

## 共通試験シナリオ T00–T14

各IDを同じ順序・同じ6項目で実施する。$RT/$LOG/$BUS-PORT/$CHANNEL_PLANはrunごとの実値に置換し、空のcandidate専用runtime/logを使う。物理操作を依頼する各回は既定通知を行い、最大5分で応答がなければ未完了として停止する。OS/API固有の長い操作は同じT-IDから後続recipeを参照する。

### T00 候補/配布物固定

- **適用対象/頻度:** 毎releaseのcandidate作成時。
- **準備:** source SHA、CI run/attempt、archive targetとhashを固定。POSIX git rev-parse HEAD/sha256sum、Windows Get-FileHashを保存。
- **実操作/利用者への依頼:** px4d --list-jsonでartifact別実行対象を照合しsource/archive SHAを固定。
- **期待値/終了値:** provenance/hash一致の候補だけ次へ進む。候補不在は未完了。
- **停止/cleanup:** deviceを開かず、archive/logを保持。
- **記録:** source SHA、CI run/attempt、artifact ID/target/hash、取得日時、未取得理由。

### T01 実行環境・アクセス権・入力

- **適用対象/頻度:** 全native archive。初回は全modelごと。
- **準備:** E-ID/OS/ABI、USB access、firmware/model data、channel planを環境表と照合。
- **実操作/利用者への依頼:** Linux uname -a/uname -m/ldd --version、macOS sw_vers、Android getprop、Windows §6 E17 PowerShell preflight。
- **期待値/終了値:** OS/ABIがarchiveに一致し、アクセス許可/入力が揃う。不一致はdevice操作前に止める。
- **停止/cleanup:** 権限不足やdriver変更が必要なら操作せず未実施記録。
- **記録:** E-ID/host/OS/ABI/model、USB topology/permission、firmware/data、channel plan。

### T02 列挙・selector・ready確認

- **適用対象/頻度:** 毎release、対象model/device。
- **準備:** 候補archiveのlist-json/models入口とdevice selectorを準備しdaemon専用runtimeを作る。
- **実操作/利用者への依頼:** px4d --list-jsonを保存しUSB IDを選ぶ。px4d --device "$ID" --firmware "$FW" --runtime-dir "$RT"を起動し、px4ctl --device "$ID" --runtime-dir "$RT" statusとlistを実行する。Androidは§6の2-FD起動recipeを使う。
- **期待値/終了値:** JSON/portが一意、status ready、receiver数が公開能力と一致。不正selectorは成功でない。
- **停止/cleanup:** daemon通常停止しprocess/FD/IPC/socket残留を確認。
- **記録:** JSON、selector、status/list、各exit、residual。

### T03 最大同時受信

- **適用対象/頻度:** 毎release、各modelの公開最大能力。
- **準備:** 各公開receiver/deviceを一意に割り当て、各clientに別TS出力/ログを用意。
- **実操作/利用者への依頼:** 各対象modelでpx4ctl --device "$ID" --runtime-dir "$RT" listを実行し、公開receiver数分のpx4-tsを別TS出力で同時起動して各PIDを個別waitする。Q3U4では§0.1 X-LOADの8 receiver（0/1/4/5=S、2/3/6/7=T）を使う。
- **期待値/終了値:** 各clientの稼働時間が重なり、各exit 0とTS/counter合格。順次取得を同時試験の代用にしない。
- **停止/cleanup:** 全client通常終了後daemon通常停止、残留なし。
- **記録:** model/receiver/system/channel、各start/end/exit、TS/counter、cleanup。

### T04 不足波・全無料channel

- **適用対象/頻度:** 毎releaseは不足波。初回/majorは全機種の代表機で全無料channel各10秒。
- **準備:** 現地channel planとmodel能力から対象system/channel一覧を作る。
- **実操作/利用者への依頼:** px4-ts --device "$ID" --receiver <R> --system <SYSTEM> --freq <FREQ> [--slot <SLOT>] --runtime-dir "$RT" --time 10 --output <FILE>。
- **期待値/終了値:** 各対象channelのTS/exit合格。未取得は未完了、非対応波だけ根拠付きN/A。
- **停止/cleanup:** client wait/exit保存、daemon通常停止。
- **記録:** model/system/channel/全channel list、TS解析/counter/exit、未取得理由。

### T05 受信中の状態確認・制御

- **適用対象/頻度:** 毎release。CARD/controlは製品能力に従う。
- **準備:** capture clientを起動し、別terminalでstatus/list/controlを行えるようにする。
- **実操作/利用者への依頼:** 受信中に別terminalからpx4ctl --device "$ID" --runtime-dir "$RT" statusとlistを実行し、対応modelでは同CLIのcard-apdu 90:30:00:00:00を10回個別送信して各終了値を保存する。
- **期待値/終了値:** 照会/control時刻がstreamと対応し各response/exitを保存。機能外操作はN/A。
- **停止/cleanup:** client/daemonを通常停止し残留確認。
- **記録:** 照会/control時刻、response class、各exit、streamとの重なり。

### T06 CARD物理挿抜・復帰

- **適用対象/頻度:** CARD reader/status対応model。毎release。
- **準備:** capture停止・daemon維持。初期CARD状態とreader有無を確認。
- **実操作/利用者への依頼:** daemon維持/capture停止。§0.1 X-CARD-HPでinitial status/ATR/reset/APDU、抜去/再挿入を別依頼し各状態確認。
- **期待値/終了値:** 抜去後statusはabsent、ATR/APDUはCARD_REMOVEDまたはNO_CARD (exit 9)。再挿入後status/ATR/reset/APDUが復帰し、各通常操作はexit 0。CARD readerを持つmodelに限り実施する。応答なしは未完了。raw ATR等は保存しない。
- **停止/cleanup:** daemon通常停止、FD/IPC/runtimeを確認。
- **記録:** 依頼時刻、状態/response/exit、復帰、cleanup。

### T07 USB待機中・受信中の切断と再取得

- **適用対象/頻度:** 毎release。USB対応deviceのidle/streamを別run。
- **準備:** `px4d --list-json`でUSB pathを特定し、streamなしのdaemonを起動してstatus/list、daemon PIDとUSB owner状態を記録する。有限capture時間を明示設定する。
- **実操作/利用者への依頼:** daemon保持中のidleに利用者へ抜去を依頼し、応答後にstatus/list、daemon診断と実際の終了値を採取する。daemonを通常停止し、再接続を依頼して再列挙したpathで新runtime/daemonを起動し、status/listと短い`px4-ts` captureを実行する。別runではfinite capture中に抜去を依頼し、client/daemon診断と各実終了値を採取する。
- **期待値/終了値:** idle時は保持中USBの切断がstatus/診断に現れ、process終了値は実値を記録する。stream時は切断診断とclient exit 7を確認する。time満了exit 0は切断合格でない。新daemonの短TSはexit 0とTS/counter合格。
- **停止/cleanup:** idle/streamそれぞれでowned client/daemonを通常停止してwaitし、再接続後の新daemonも通常停止する。Androidはpermission/FD再取得。
- **記録:** idle/stream、daemon USB owner状態、依頼/応答時刻、path、status/診断、各実終了値、再取得TS/counter、cleanup。

### T08 通常停止・cleanup

- **適用対象/頻度:** 全test終了時。
- **準備:** 各owned client/daemon PIDとruntimeを記録してから起動する。
- **実操作/利用者への依頼:** §0.1 X-STOP/X-RESIDでclient wait、daemon EOF/SIGINT、owned PID cleanup。
- **期待値/終了値:** 全owned process終了、TS close、FD/IPC/socket/runtime残留なし。timeoutはfail。
- **停止/cleanup:** 無関係processへsignalしない。test専用runtimeだけ片付け失敗log保持。
- **記録:** PID/role/exit、process/FD/IPC/socket/runtime、復旧状態。

### T09 TS・counter・終了値判定

- **適用対象/頻度:** 全capture/soak。
- **準備:** TS parser/SPEC条件とdaemon counterを確認できるようにする。
- **実操作/利用者への依頼:** SPEC parser/sync/TEI/continuity/drop/USB countersを各receiver TSごと確認。
- **期待値/終了値:** 全TSが製品SPEC基準を満たし通常取得exit 0。非空file/counter 0だけでpassにしない。
- **停止/cleanup:** 元TS/hash/logを残してT08 cleanup。
- **記録:** parser/version、TS hash、sync/TEI/continuity/drop/counter、exit。

### T10 native PC/SC・WinSCard

- **適用対象/頻度:** 対応OS/APIのみ。非対応製品は理由付きN/A。
- **準備:** native OS adapter/consumerとcandidate IFD/DLL pathを用意。
- **実操作/利用者への依頼:** Linux/macOS native IFDとWindows §6 E17/Windows WinSCard recipeでcandidate DLL、consumer、reader/card transactionを確認。
- **期待値/終了値:** CLI CARD/CI smokeをnative transaction合格にしない。実consumer/adapter/path/exitが必要。
- **停止/cleanup:** consumer/isolated pcscd通常停止。system PCSC serviceを変更しない。
- **記録:** OS/ABI、module/DLL absolute path、consumer、reader/card、operation/exit。

### T11 LNB・非対応要求の拒否

- **適用対象/頻度:** 必要時。給電対応profileだけ実測。
- **準備:** 対応profile、meter、独立給電の状態を確認。
- **実操作/利用者への依頼:** profile capability手順で非対応PX-M1URの--lnb-voltage 15拒否exit 3。対応機meter testは指定条件のみ。
- **期待値/終了値:** 対応はmeter実測、非対応は拒否しGPIO/給電変化なし。
- **停止/cleanup:** daemon停止、0Vと通常配線復帰。非対応機へ給電しない。
- **記録:** model/profile、command/exit、meter値/条件、cleanupまたはN/A根拠。

### T12 複数device・daemon・access

- **適用対象/頻度:** 必要時、複数device/selector/access変更時。
- **準備:** USB port/selectorとinstance/runtimeをdeviceごとに割り当てる。
- **実操作/利用者への依頼:** px4d --list-jsonから複数pathを識別。曖昧selectorの既存拒否例はexit 2、claim前であることを確認。
- **期待値/終了値:** device/selector対応一意。別deviceの結果を混同せず、要求時は各sideを独立判定。
- **停止/cleanup:** 各client/daemon個別wait/stopしFD/IPC/socket確認。
- **記録:** device/port/PID/runtime、各exit/継続/再取得/access/cleanup。

### T13 連続受信・頻度

- **適用対象/頻度:** 毎releaseおよび全model初回/major。
- **準備:** 確定頻度表の対象archive/model/timeをmatrixへ展開する。
- **実操作/利用者への依頼:** 毎release代表Linux x86_64 glibc 2h/全archive native short; 初回全model全無料channel 10s、macOS/Windows 30m、他ABI 10m。
- **期待値/終了値:** 指定連続時間完了かつTS条件合格。追加soakは明示ユーザー決定分のみ。
- **停止/cleanup:** T08通常stopと全resource cleanup。
- **記録:** model/OS/ABI/archive、時間、TS/counter、FD/RSS、未実施理由。

### T14 失敗・リリース判定

- **適用対象/頻度:** 全candidate gate完了後。
- **準備:** source/archive SHAと全T-IDの結果を版別記録へ用意。
- **実操作/利用者への依頼:** version recordでT-ID/claim結果を分離。failure/未認定をpassにせず正式releaseは別依頼。
- **期待値/終了値:** failure/unsupported/未実施/環境不能/candidate欠落をpassにしない。
- **停止/cleanup:** candidateと失敗証拠を保持。tag/releaseは別依頼。
- **記録:** T-ID、candidate/host/model、state、log、blocker、判定根拠。

N/Aは製品機能上該当しない項目だけに使う。OS/ABI候補のunsupported、candidate欠落、未実施、失敗、環境不能は版別結果で未達とし、passに読み替えない。手順に記した期待値は実測結果ではない。

## 対象アーカイブ・環境から全試験への対応

選択したarchive/E-IDごとに、下表のT00–T14を同じrun記録へ実施する。各T見出しは準備・実操作・期待値/終了値・停止/cleanup・記録を含む手順本体であり、表は対象環境からその全項目へ直接辿る索引である。環境表に載る別OS/ABIの結果や過去runで代用しない。

| archive / E-ID / 実行環境 | T00–T14の手順リンク |
|---|---|
| `linux-glibc-x86_64` / [E03](#e03-anduinos-x8664基準linux-x8664) / Linux x86_64/glibc | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `linux-musl-x86_64` / [E02](#e02-haos-x8664-alpinemusl-addon基準環境supervisor) / Linux x86_64/musl | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `linux-glibc-aarch64` / [E15-G](#e15g-e15m-linux-aarch64glibc-musl-native確認環境) / Linux aarch64/glibc | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `linux-musl-aarch64` / [E15-M](#3-配布アーカイブ別手順) / Linux aarch64/musl | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `darwin-arm64` / [E04](#e04-macos-arm64基準darwin環境) / macOS arm64 | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `android-aarch64` / [E05](#e05-android-termux-aarch64基準androidaarch642fd) / Android Termux/aarch64 | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `android-armv7a` / [E06](#e06-android-termux-armv7a基準androidarmv7a2fd) / Android Termux/armv7a | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `android-x86_64` / [E07](#e07-bliss-os-x8664-termux基準androidx86642fdnative確認環境例) / Android Termux/x86_64 | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `windows-x86_64` / [E17](#e17-windows-11-x8664windowsアーカイブのnative確認phase-12) / Windows 11 x86_64 | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |

OS/ABI固有の起動方法は以下の環境recipeと§3のarchive手順を使う。試験の期待・cleanup・記録項目はリンク先T本文に従う。機能対象外だけを理由付きN/Aとし、候補未入手・unsupported・未実施・環境不足は未達で記録する。

## 0. 共通配布アーカイブと共通手順

各冊の「共通実行環境表」は、Stable release gateで確認する正式配布artifactとnative/canonical runtimeの対応matrixである。環境IDはOS/ABIとUSB access boundaryを識別し、host名や試験状態を表さない。正式な対象archiveはREADME/SPECと各版のmatrixに従う。

| 共通要素 | 役割 |
|---|---|
| 環境ID | OS/ABIとruntime/access条件の識別子。host名・実施状態は版別記録に残す。 |
| 配布artifact matrix | 上の環境表で正式archiveと試験環境を対応付ける。追加の移植性確認環境はこのmatrixへ含めない。 |
| T00–T14 | 共通の試験項目・操作・期待値・cleanup・記録項目を示す。 |
| 共通コマンド | CLIやOS固有の実コマンドは製品ごとに異なる。各環境表の「配布artifact / 手順」リンク先と続くrecipeを使い、製品間で同じCLIを仮定しない。 |
| 状態語彙 | 実施状態は下の共通語彙を版別記録で使う。試験結果の合否とは別に記録する。 |

### 検証状態の共通語彙

| 状態 | 意味 |
|---|---|
| `継承` | 同一model・runtime/access path・featureで、artifact同一性または非影響の根拠がある既存証拠を適用する。 |
| `今回再検証` | 今回のcandidateで対象を実行し、結果を記録する。 |
| `未認定` | 対象の現行証拠がなく、合格を示せない。 |
| `対象外` | 製品仕様の対象外に限る。未実施・環境不能・candidate欠落・失敗には使わない。 |

## PX4固有の配布アーカイブ手順とコマンド

### 配布artifact別の短時間実機手順

以下は確立済みのartifact別操作手順である。candidateに対する適用頻度と版固有matrixは該当版の `docs/release-validation/<version>.md` に従う。実施対象となるarchiveは候補artifact自体をnative architectureで起動し、別archive/OSの結果で置き換えない。Linux musl aarch64はE15のnative Fedora aarch64上でAlpine arm64 Docker containerを使い、CPU emulationなしで実施する。

| 配布binary archive | 環境例 | 選択されたcandidateで行う実機操作 |
|---|---|---|
| `linux-glibc-x86_64` | E03 AnduinOS x86_64 | 列挙、B-CAS抜去・再挿入（不在検出、ATR/reset/APDU復帰）、8 receiverの短い受信、USB切断・再接続と復旧確認 |
| `linux-musl-x86_64` | E02 HAOS Alpine/musl add-on | 同上。試験用add-onの起動停止とSupervisor設定復元を含む |
| `linux-glibc-aarch64` | E15-G Fedora aarch64 / L4T | 同上。native artifactを使う |
| `linux-musl-aarch64` | E15-M Fedora aarch64上のAlpine arm64 Docker container | 同上。native aarch64 host上で実行し、CPU emulationを使わない。物理USB deviceをcontainerへ渡す |
| `darwin-arm64` | E04 macOS arm64 | 同上。native `darwin-arm64` artifactを使う |
| `android-aarch64` | E05 Termux aarch64 | 同上。2-FD launcher経由で受信する |
| `android-armv7a` | E06 Termux armv7a | E05と同じ手順、armv7a archive |
| `android-x86_64` | E07 Bliss OS Termux x86_64 | E05と同じ手順、x86_64 archive |
| `windows-x86_64` | E17 Windows 11 x64 | E17のWindows手順。列挙・ready、短い受信、CARD ATR/reset/反復APDU、カード抜去・再挿入、USB切断・再接続後のdaemon再起動、cooperative shutdown後の残留確認 |

このmatrixはrelease archive manifestの9 binary target（8 tar archiveとWindows ZIP 1つ）と、Q3U4の既存実機記録に基づく。過去の受信・抜差し結果は[validation-results.md](platforms/validation-results.md)に記録されている。[PR #15](https://github.com/Khronos31/px4-userland/pull/15)はfinal candidate artifact auditとhardware canaryを記録し、[Issue #4](https://github.com/Khronos31/px4-userland/issues/4)はusbip・LSM・libusb/access pathなどディストリビューション名だけでは覆えない検証軸を整理している。過去記録は今回candidateのmatrix結果を代替しない。

各artifactの実機確認は列挙、短い受信、カード状態確認、B-CAS抜去/再挿入、USB切断/再接続、復旧確認を順に実行する。これら一連の確認に総時間上限を設けない。5分の上限はユーザーへ物理操作を依頼してから操作が完了するまでの待機だけに適用する。候補archiveのchecksum・展開、APDU、RF/電源/firmware、host準備は検証開始前に済ませる。5分以内に物理操作が行われなければ、その操作を未完了として記録し、物理deviceを使う工程を中断する。5分を超える連続負荷試験は短時間確認に混ぜず、ユーザーが決定するsoakへ分ける。

canonical runtime は、そのartifactの実機確認を代表する環境を指す。同一executableを共有するruntimeでもaccess pathが異なれば、証拠は自動継承しない。

### 0.1 共通コマンド

`<version>`、`<platform>`、`$ID`、`$INSTANCE`、`$FW`、`$RT`、`$LOG` は実値に置き換える。artifact は候補 commit の `release-candidate`（§2）だけを使い、手元 build や旧 archive で代用しない。

artifact 検証と展開（X-PREP）:

```sh
sha256sum -c SHA256SUMS                              # macOS: shasum -a 256 -c
D=$(mktemp -d); tar -xzf px4-userland-<version>-<platform>.tar.gz -C "$D"
sha256sum "$FW"                                      # 5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484
RT=$(mktemp -d "${TMPDIR:-/tmp}/px4-userland.XXXXXX"); LOG=<log dir>
ID=usb:<BUS-PORT>
INSTANCE=<instance-token>
"$D/px4d" --list-json > "$LOG/discovery.json"         # USB discovery JSON; inspect enclosures[].devices[] and receivers[]
```

daemon 起動と準備完了確認（X-START）:

```sh
"$D/px4d" --device "$ID" --instance "$INSTANCE" --firmware "$FW" --runtime-dir "$RT" 2> "$LOG/px4d.err" &
P=$!
# statusを再実行してreadyを確認する。group modeならpx4d/px4ctlの両方に --group を付ける。
"$D/px4ctl" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" status | tee "$LOG/status-start.txt"
"$D/px4ctl" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" list | tee "$LOG/ctl-list.txt"
```

受信と監視（X-LOAD / X-MON）:

USB selectorを指定するdaemonには`--instance`が必要である。clientにもdaemonと同じdevice selector、instance、runtime directoryを指定する。短い単一receiver確認には次の形を使い、`<SECONDS>`は該当版のmatrix値へ置き換える。

```sh
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --receiver <ID> --channel T27 \
  --runtime-dir "$RT" --output t27.ts --time <SECONDS>
```

Q3U4の8 receiver確認は次の8コマンドを同じ端末で個別に実行する。各receiverは30秒で終了する。これは受信確認であり、物理操作の応答を待つ間の負荷継続には使わない。

```sh
# Q3U4 8 receiver（受信継続: 0/1/4/5=S、2/3/6/7=T）
"$D/px4-ts" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 0 --channel BS15_0 --lnb-voltage 0 --time 30 --output "$LOG/r0.ts" 2> "$LOG/r0.err" & R0=$!
"$D/px4-ts" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 1 --channel BS15_0 --lnb-voltage 0 --time 30 --output "$LOG/r1.ts" 2> "$LOG/r1.err" & R1=$!
"$D/px4-ts" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 4 --channel BS15_0 --lnb-voltage 0 --time 30 --output "$LOG/r4.ts" 2> "$LOG/r4.err" & R4=$!
"$D/px4-ts" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 5 --channel BS15_0 --lnb-voltage 0 --time 30 --output "$LOG/r5.ts" 2> "$LOG/r5.err" & R5=$!
"$D/px4-ts" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 2 --channel T27 --time 30 --output "$LOG/r2.ts" 2> "$LOG/r2.err" & R2=$!
"$D/px4-ts" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 3 --channel T27 --time 30 --output "$LOG/r3.ts" 2> "$LOG/r3.err" & R3=$!
"$D/px4-ts" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 6 --channel T27 --time 30 --output "$LOG/r6.ts" 2> "$LOG/r6.err" & R6=$!
"$D/px4-ts" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 7 --channel T27 --time 30 --output "$LOG/r7.ts" 2> "$LOG/r7.err" & R7=$!
# 8個それぞれの終了値を記録する（各waitは個別に実行）。
wait "$R0"; echo "$?" > "$LOG/r0.rc"
wait "$R1"; echo "$?" > "$LOG/r1.rc"
wait "$R4"; echo "$?" > "$LOG/r4.rc"
wait "$R5"; echo "$?" > "$LOG/r5.rc"
wait "$R2"; echo "$?" > "$LOG/r2.rc"
wait "$R3"; echo "$?" > "$LOG/r3.rc"
wait "$R6"; echo "$?" > "$LOG/r6.rc"
wait "$R7"; echo "$?" > "$LOG/r7.rc"
```

受信中の監視は別の端末で実行する。shell変数は端末間で引き継がれないため、起動端末に表示されたcandidate展開先 `$D`、device selector `$ID`、`$INSTANCE`、`$RT` と同じ実値をこの端末にも設定する。

PX4の`card-apdu HEX`は単発送信である。反復確認は既存標準照会を通常コマンドで10回実行する。

```sh
D=<candidate-directory>
ID=usb:<BUS-PORT>
INSTANCE=<instance-token>
RT=<same-runtime-directory-as-startup-terminal>
LOG=<same-private-log-directory>
"$D/px4ctl" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" status
for i in 1 2 3 4 5 6 7 8 9 10; do
  if "$D/px4ctl" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-apdu 90:30:00:00:00 > "$LOG/card-$i.apdu"; then rc=0; else rc=$?; fi
  echo "$rc" > "$LOG/card-$i.rc"
done
```

FD/RSSも記録する。受信確認の実行時間は30秒で、全手順やartifact gateの所要時間を制限しない。

Termux での起動（X-START-TERMUX）:

```sh
# Termux 2-FD（E05–E07）
RT="$PREFIX/tmp/p4"; mkdir -p "$RT"; chmod 700 "$RT"
"$D/px4-termux" --usb-device <path1> --usb-device <path2> --device "$ID" --firmware "$FW" --runtime-dir "$RT"
# 別セッションで §0.1 の px4d status確認コマンドをreadyになるまで再実行し、px4-ts / px4ctlを同じ $ID と $RT で実行する
```

終了と残留確認（X-STOP / X-RESID）:

```sh
kill -TERM $P; wait $P           # 0 を期待
pgrep -fl '[p]x4d'; rmdir "$RT"   # 残存 process / endpoint なし
```

Q3U4 preflight: 両 USB device、15V adapter、両 RF lead を接続し、`px4d --list-json`の`enclosures[].devices[]`に両USB deviceと合計8 receiverが列挙されることを確認する。daemon起動後は`px4ctl status`のreadyと`px4ctl list`の8 receiverを確認してから開始する。

選択された短時間確認におけるカード物理抜去・再挿入（X-CARD-HP）:

以下は§0.1のdaemonを起動したまま、receiver captureを行っていない状態で実施する。HAOS/SCS側で各物理操作を依頼する直前に通知コマンドを実行する。Codexは`beep`、Claude Codeは`vibe`を使い、通知後にユーザーへ操作を依頼してから最大5分待つ。5分はその物理操作1回の応答待ちだけに適用する。要求した操作が未完了ならその操作とartifact gateを未完了として記録し、次へ進まない。

カード挿入状態を確認するコマンド群（個別に実行）:

```sh
CTL="$D/px4ctl"
APDU=90:30:00:00:00
"$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-status | tee "$LOG/card-before.txt"
"$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-atr > "$LOG/card-before.atr"
"$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-reset > "$LOG/card-before.reset"
for i in 1 2 3 4 5 6 7 8 9 10; do
  if "$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-apdu "$APDU" > "$LOG/card-before-$i.apdu"; then rc=0; else rc=$?; fi
  echo "$rc" > "$LOG/card-before-$i.rc"
done
```

カード抜去を要求する前にHAOS/SCS側で通知する:

```sh
beep   # Codex。Claude Codeは vibe
```

ユーザーへB-CASカードを抜くよう依頼し、操作完了まで最大5分待つ。その後に不在検出コマンドを個別に実行する:

```sh
"$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-status | tee "$LOG/card-removed.txt"
if "$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-atr > "$LOG/card-removed.atr" 2> "$LOG/card-removed.err"; then rc=0; else rc=$?; fi
echo "$rc" > "$LOG/card-removed.atr.rc"       # NO_CARD / exit 9 を期待
if "$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-apdu "$APDU" > "$LOG/card-removed.apdu" 2> "$LOG/card-removed-apdu.err"; then rc=0; else rc=$?; fi
echo "$rc" > "$LOG/card-removed.apdu.rc"      # NO_CARD / exit 9 を期待
```

カード再挿入を要求する前にHAOS/SCS側で再度通知する:

```sh
beep   # Codex。Claude Codeは vibe
```

ユーザーへB-CASカードを戻すよう依頼し、操作完了まで最大5分待つ。その後に復帰コマンドを個別に実行する:

```sh
"$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-status | tee "$LOG/card-reinserted.txt"
"$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-atr > "$LOG/card-reinserted.atr"
"$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-reset > "$LOG/card-reinserted.reset"
for i in 1 2 3 4 5 6 7 8 9 10; do
  if "$CTL" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-apdu "$APDU" > "$LOG/card-reinserted-$i.apdu"; then rc=0; else rc=$?; fi
  echo "$rc" > "$LOG/card-reinserted-$i.rc"
done
```

判定: `card-status`の`present=no`とreader generationの変化、ATR/APDUの`NO_CARD`・exit 9を確認する。再挿入時は`present=yes`となりgenerationが再度変化し、ATR取得・reset・APDU 10回が成功することを確認する（過去の正常応答はSW `90 00`）。その後§0.1の8 receiverを30秒実行し、TS/counter条件はSPEC 10.2に従う。カード抜去・再挿入ができない場合、そのartifactの短時間gateは未完了。

選択された短時間確認におけるUSB物理切断・再接続（X-USB-HP）:

カードを挿入した状態でdaemonを起動し、8 receiver captureは止めておく。USB切断を観測するためのclientを1つだけ起動する。HAOS/SCS側で物理操作を依頼する直前に通知し、操作完了まで最大5分待つ。要求した操作が未完了ならその操作とartifact gateを未完了として記録し、次へ進まない。Q3U4は筐体の物理USBケーブル1本を外すと、2つの内部USB deviceが切断される。

USB切断を観測するclientを起動する:

```sh
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --receiver 0 --system isdb-s --freq 1318000000 --slot 0 \
  --runtime-dir "$RT" --output /dev/null --time 300 2> "$LOG/usb-detach-client.err" &
USB_CLIENT=$!
```

USB切断を要求する前にHAOS/SCS側で通知する:

```sh
beep   # Codex。Claude Codeは vibe
```

ユーザーへ対象tunerのUSB接続を外すよう依頼し、操作完了まで最大5分待つ。5分以内に操作がなければclientがduration満了で終了していても、物理操作とartifact gateを未完了として記録する。まだclientが動いている場合は停止する。USB切断によりclientが終了した場合は、終了値を記録して検査する（`DISCONNECTED` / exit 7を期待）:

```sh
wait "$USB_CLIENT"; echo "$?" > "$LOG/usb-detach-client.rc"
"$D/px4ctl" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" status > "$LOG/usb-detach-status.txt" 2>&1
```

切断したUSB接続を戻すようユーザーに依頼する直前に、HAOS/SCS側で再度通知する:

```sh
beep   # Codex。Claude Codeは vibe
```

ユーザーへUSB接続を戻すよう依頼し、操作完了まで最大5分待つ。再列挙・復旧コマンドを個別に実行する:

```sh
"$D/px4d" --list-json > "$LOG/discovery-reconnected.json"
```

切断側clientが有限時間内に`DISCONNECTED` / exit 7で終了し、ハングやclient process残留がないことを確認する。過去の実機確認では切断後も旧daemonが生存し、USBを戻して再列挙されても自動復帰しなかった。`px4d --list-json`で対象device（Q3U4は2つの内部USB device）が再列挙されたことを記録するが、これはdaemon復旧の合格とはしない。daemon再起動後は`px4ctl status`でready、`px4ctl list`でreceiverを確認する。

旧daemonが生存している場合は、同じ候補のdaemonを停止して終了を待つ:

```sh
kill -TERM "$P"
wait "$P"
```

旧daemonがすでに終了している場合は`pgrep -fl '[p]x4d'`で残留がないことを確認する。どちらの場合も旧daemonを再利用せず、X-STARTのコマンド群を同じcandidate artifact・device ID・instance・runtime dirで再実行し、`"$D/px4ctl" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" status`でreadyを確認する。その後§0.1の8 receiver 30秒確認と、次のAPDU確認を行う:

```sh
for i in 1 2 3 4 5 6 7 8 9 10; do
  if "$D/px4ctl" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" card-apdu 90:30:00:00:00 \
    > "$LOG/card-after-usb-reconnect-$i.apdu"; then rc=0; else rc=$?; fi
  echo "$rc" > "$LOG/card-after-usb-reconnect-$i.rc"
done
```

終了後にX-STOP / X-RESIDでdaemon/client process、FD、IPC socket/control endpointの残留がないことを確認する。USB切断・再接続ができない場合、そのartifactの短時間gateは未完了。

この再起動ベースの復旧は[既存の実機記録](platforms/validation-results.md)に基づく。Q3U4のHAOSでのケーブル再接続後の再列挙（[記録](platforms/validation-results.md#L37)）、Alpine muslの物理切断後の手動復帰（[HAOS add-on](platforms/validation-results.md#L263)）、OpenWrtでの切断exit 7・残留なし・再接続後の復帰（[記録](platforms/validation-results.md#L270)）、PX-M1UR/PX-S1URでの旧daemon停止後の復旧（[M1UR](platforms/validation-results.md#L141)、[S1UR](platforms/validation-results.md#L142)）がある。

### 0.2 同一lease再選局（条件付き）

同一lease retuneは、IPC/lease/retune、frontend/tune、device identityの変更時に追加確認する。それ以外はCI offline試験の成功を記録する。hardwareで行う場合は、既存のout-of-repo `/config/.work/px4-m1ur-s1ur/retune-tool/retune_tool.cpp`を使ってよい。その場合はsourceとbinaryのSHA-256、およびbuild条件（compiler、flags、libusb）をrelease recordに記録する。新しいtoolをこのrepositoryへ追加しない。

### 0.3 receiver 7 参照比較（X-R7REF、条件付き）

receiver 7 で TEI/continuity burst が出て、SPEC 10.2.6a の比較条件（同一個体・antenna・power・firmware・frequency・duration・同等 capture）を満たす fresh 参照が必要な場合にだけ行う。追加の移植性確認環境でこの比較を行う場合の既存条件は[移植性確認手順](platforms/portability-checks.md)に記録する。kernel module の load/unload はrelease gateの要件に含めない。比較条件を再現できない場合は判定保留とし、pass 扱いしない。


## 1. 候補と前回証拠を固定する

1. 候補の `VERSION`、GitHub 上の source commit、candidate workflow run を特定する。候補固定前に同版の `release-notes/vX.Y.Z.md` / `.json` の無効なplaceholderだけを用意し、source inventoryへ含める（[`release-notes/README.md`](../release-notes/README.md)）。ノート本文や合格を先に書かない。dirty な作業ツリーや未 push のローカル変更を候補として試験しない。
2. 前回 Stable 以降の累積差分と、今回使う baseline evidence を確認する。差分をファイル名だけで判断せず、SPEC 10.5.1 の変更分類と実際の呼出経路・依存先から model/profile・runtime/access path・feature の影響範囲を決める。
3. 対象 claim ごとに `継承`、`今回再検証`、`未認定`、`対象外` のいずれかを記録し、根拠を書く。証拠の軸は SPEC 10.2.8 に従う。別 model、別 OS/runtime、別 access path、別 feature へ結果を外挿しない。同一 model・同一 runtime/access path・同一 feature の証拠は、対象 artifact の bytes が baseline と同一であるか、変更がその path へ影響しないと 10.5.1 の表で判定できる場合に継承できる。未知の影響は affected として扱い、`対象外` は SPEC の対象外または本手順 §0 の対象外に限る。
4. 前回の適格なlong soakの日付やrelease数は事実情報として記録できるが、エージェントがsoak有無・時間・OSを決める規則として使わない。

Termuxのarchitecture、Termux launcherのFD path、glibc/musl、native PC/SC adapterは別々のruntime/access pathとして扱う。Windows 11 x64（E17）のcandidate適用頻度は該当版のmatrixに従い、WindowsとWinSCardが対象に含まれる版ではnative libusb tuner、CARD_*、WinSCard、実機の物理回帰を以下の手順で行う。CIのbuild/offline test成功は`build-tested`のみを示し、実機matrixが未完了の間は当該行を`未認定`（`hardware-unverified`）として扱う。追加移植性確認は[移植性確認手順](platforms/portability-checks.md)に分離し、Android ad-hoc APKはdtv-android所管である。変更も新規claimもないpathを毎回試験しない。

影響分類が複数にまたがる、依存範囲が不明、または非影響を証明できない場合は `unknown/ambiguous` として、影響し得るpath集合をユーザーへの説明に含める。選択する短時間実機確認のarchiveは影響判定に関係なくcandidateそのものを使い、適用するarchive/OS頻度は該当版の `docs/release-validation/<version>.md` に従う。

## 2. source / CI / 候補アーカイブ判定（毎回）

1. 候補 commit に対応する `portable userland foundation` workflow を確認する。path filter 等により自動実行されていなければ、GitHub Actions の `workflow_dispatch` でその候補 ref を指定して実行する。
2. workflow 内の unit/offline test、各 target build、source/relink、`release-candidate` と、それに依存する4つの Ubuntu/Alpine × x86_64/aarch64 artifact smoke job、Windows cross-build/PE audit/packaging job、および同じWindows ZIPを実行するWindows native offline test job がすべて成功していることを確認する。失敗 job を無視して先へ進まない。`release-candidate` はWindows cross-buildとnative testの両方に依存し、native testが消費したものと同一のWindows ZIPを取り込む。test executable artifactは取り込まない。
3. `release-candidate` artifact が候補 commit の `source_ref` を示し、9 binary archive（8 tar archiveとWindows ZIP 1つ）と対応 source archiveの計10 archive、およびその外側 `SHA256SUMS`（10件）を含むことを確認する。チェックサムを照合し、CIの manifest・license・corresponding-source・binary/source archive audit（Windows ZIPのPE/import/archive監査を含む）が通っていることを確認する。
4. 工程2の試験完了前に候補source commitが変わった場合は、そのcommitのCIとartifactを取り直す。前のcommitのarchive、手元で別途作ったbuild、PR runの古いartifactを新candidateの代用にしない。工程3の試験結果commitは候補source commitの変更ではなく、工程4ではその結果commitにタグを付けてrelease CIを実行する。
5. tag CIが生成した9 binary archiveを、実機試験に使ったfinal candidate archiveと比較する。inventory/type/mode/linkと全非文書payloadが一致することを要求する。binaryは更新された`README.md`、それを反映した`manifest.json`の`source_ref`およびREADMEのhash/size、内側`SHA256SUMS`だけを許す。source archiveは結果commitの明示的に許可された文書、commit/tree identity、それらのmanifest項目とchecksumだけを許す。source側のノート・記録も同じinventoryを維持する。公開workflowの共通検査が候補2runとtagを照合する。範囲と停止条件は [`releasing.md`](releasing.md)、記録は [`release-record.md`](release-record.md) に従う。

手動dispatchとartifact取得には次の短いCLI手順を使える。`<candidate-ref>`、`<run-id>`、`<new-empty-dir>`を実際の値へ置き換え、artifactは新しい空ディレクトリへ展開する。既に成功済みの自動runが候補commitと一致するなら再dispatchしない。

```sh
gh workflow run build_userland.yml --ref <candidate-ref>
gh run list --workflow build_userland.yml --limit 10
gh run view <run-id> --json headSha,conclusion
gh run download <run-id> --name release-candidate --dir <new-empty-dir>
(cd <new-empty-dir> && sha256sum -c SHA256SUMS)
```

CI が行う build・audit・smoke を成功後に同じ目的でローカル再実行しない。CIで失敗または未実施の項目がある場合に限り、原因調査に必要な既存コマンドを実行する。新しい汎用検証スクリプトは作らない。

SPEC 10.5-2の再現性確認は、final candidateと同一source commitに対する独立した2回のclean CI candidate runで、9 binary archive（8 tar archiveとWindows ZIP 1つ）、corresponding-source archiveの10 archiveすべてのSHA-256が一致し、各archive本体がbyte-identicalであり、かつ外側`SHA256SUMS`自体もbyte-identicalであることによる。Windows ZIPもtar archiveと同じbyte一致比較の対象とし、ZIP内部のtimestamp正規化だけによる合格は認めない。比較は、§2のCLI手順で候補commitの`release-candidate` runを2本分取得してから、次の手順で行う。

1. 1本目のartifactを新しい空ディレクトリ`<run1-dir>`へ、2本目のartifactを別の新しい空ディレクトリ`<run2-dir>`へ展開する。既に成功済みの同一commit runを1本目に使ってよい。
2. 各ディレクトリで`sha256sum -c SHA256SUMS`を実行し、10件すべてがOKであることを確認する。
3. 両runの10 archive（Windows ZIPを含む）を個別に`cmp`で比較し、外側`SHA256SUMS`も`cmp`で比較する。

```sh
RUN1_DIR="/path/to/run1-dir"  # 展開先の実際のpathに置き換える
RUN2_DIR="/path/to/run2-dir"  # 展開先の実際のpathに置き換える
(cd "$RUN1_DIR" && sha256sum -c SHA256SUMS)
(cd "$RUN2_DIR" && sha256sum -c SHA256SUMS)
for a in "$RUN1_DIR"/*.tar.gz "$RUN1_DIR"/*.zip; do cmp "$a" "$RUN2_DIR/${a##*/}"; done  # 10 archiveのbyte一致を期待
cmp "$RUN1_DIR/SHA256SUMS" "$RUN2_DIR/SHA256SUMS"    # 一致を期待
sha256sum "$RUN1_DIR/SHA256SUMS" "$RUN2_DIR/SHA256SUMS"
```

4. checksumが一致した場合も含め、常に両runのrunner image version/IDとtoolchain識別子（compiler/SDK/NDK等）、build inputを突き合わせる。入力の一致は、pinnedされた分は同一revision/digest、workflow上floatする分は両runの実効toolchain/build inputの観測値が一致することで判定する。runner metadata（label/OS image）とartifact生成toolを区別する。実効toolchain/build inputが異なる場合、または記録から同一と確認できない場合はinconclusiveであり、成功でも失敗でもない。同一inputのmatching pairを取り直して比較するまで成功としない。runner image versionが両runで異なっても、artifact生成に用いるtoolchain/build inputが同一と確認できれば一致として扱う。
5. 両runのjob IDと結論を確認し、各artifact build jobのrunner image version/IDおよびtoolchain/build inputをCI logから記録する。`Set up job`内にrunner version・image label/versionが記録される。build jobのログでは、workflowのtoolchain inventory step、package managerの導入version、container digest、SDK/NDK versionなどを確認する。ログを次のように取得し、比較に使った原本を`$LOG`へ保存する。

```sh
gh run view <run-id> --json jobs --jq '.jobs[] | [.databaseId, .name, .conclusion] | @tsv'
gh run view <run-id> --job <job-id> --log > "$LOG/run-<run-id>-job-<job-id>.log"
```

正規化（UUID・署名・timestampのマスク等）による合格は認めない。両runのrunner image version/ID、toolchain識別子、libusb source checksumをrelease recordに残す。

比較のために常設の二重build jobや新しい検証スクリプトを追加しない。workflowの再実行はreleaseあたり1回でよい。

## 3. 配布アーカイブ別手順

適用対象archiveと頻度は該当版のmatrixに従い、選択されたarchiveは §0 の対応行と以下の操作を使ってnative環境で独立に実施する。Windows行はE17のWindows手順に従い、native Windows 11 x64で実施する。必須確認の一連の操作に総時間上限は設けない。B-CAS/USBの物理操作はユーザーが行い、各操作を依頼した時点から最大5分待つ。各物理操作を依頼する直前にHAOS/SCS側でCodexは`beep`、Claude Codeは`vibe`を実行する。各runの記録にはarchive名/SHA-256、source commit、環境ID/OS version/architecture、機種/USB path、実施時刻、コマンド、終了値、card status/generation/ATR/APDU応答、TS/counter、残留の有無を残す。

1. 候補archiveのhash確認・展開、Q3U4のfirmware/RF/電源preflight、ログ先作成を済ませる。これらを含めて一連の確認に総時間上限は設けない。
2. `px4d --list-json`でQ3U4の両USB device、8 receiver、JSON schema・boolean型・receiver capability値を確認する。daemon起動後は`px4ctl status`でready、`px4ctl list`で8 receiverを確認する。M1UR/S1URのserial衝突やLNB非対応profile表示が変更対象なら、§0の該当する機種別追加確認も行う。
3. daemonを起動したままreceiver captureを止め、X-CARD-HPのコマンド群を実行する。カード抜去を依頼する直前に通知し、依頼後最大5分待つ。`card-status`の`present=no`、ATR/APDUの`NO_CARD`（exit 9）を確認する。
4. 再挿入を依頼する直前に通知し、依頼後最大5分待つ。generation更新、ATR/reset/APDU 10回の復帰を確認する。
5. X-USB-HPで1 receiver clientを起動してUSB切断を観測する。USB切断を依頼する直前に通知し、依頼後最大5分待つ。clientが有限時間で`DISCONNECTED` / exit 7となり、ハングやclient process残留がないことを確認する。
6. USB再接続を依頼する直前に通知し、依頼後最大5分待つ。`px4d --list-json`でUSB再列挙を確認した後、旧daemonを停止して終了を待ち、X-STARTの手順で同じcandidateからdaemonを起動し直す。古いdaemonの自動復帰を合格条件にしない。
7. 再起動後に`"$D/px4ctl" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" status`でready、`px4ctl list`で8 receiverを確認し、8 receiverを30秒実行してTS条件をSPEC 10.2の適用項目で確認する。APDU 10回も再確認し、最後にdaemon/client、FD、IPC socket/control endpointの残留がないことを確認する。

Linux musl aarch64はE15 Fedora aarch64上のAlpine arm64 Docker containerでcandidate artifactを実行する。hostがaarch64であることを`uname -m`等で確認し、CPU emulationを使わず、物理USB deviceをcontainerへ渡す。このartifact行はglibc aarch64の結果で代用せず、独立して実施・記録する。native aarch64 hostまたはUSB passthroughが利用できず物理確認を完了できない場合はgate未完了とする。

同一lease retuneは§0.2に従い、該当するコード変更時の追加確認とする。receiver 7のTEI/continuity burstは自動合格にせず、SPEC 10.2.6aの比較条件に従う。

## 4. 変更影響の追加確認と追加連続受信（ユーザー決定）

毎releaseの代表機2時間と全archive短時間matrix、および初回/major追加条件は共通表に従う。それらとは別に追加試験が必要な場合、SPEC 10.5.1の分類、hunk/call path、platform guard、過去のvalidation record、candidateで観測したcounterを使い、影響し得るmodel/profile/runtime/access pathを整理する。この整理は追加soakの有無やtarget OSをエージェントが決める権限を与えない。

安定性に影響し得るコード変更があれば、エージェントは次をユーザーへ提示し、回答を得るまでsoakを開始しない。

- 変更と安定性への具体的な影響経路、および影響し得るOS/artifact/機種
- 既存証拠で今回分かること、今回の短時間matrixで得た結果、未確認点
- 追加soakの選択肢: 実施なし / 指定時間。実施する場合は対象OSもユーザーが指定

ユーザーは追加soakを行わない選択もできる。選択された時間とOSを記録し、その指定範囲だけ試験する。agentはdiff class、canonical順、影響範囲、機材都合から追加soakの時間・有無・OSを既定または自動決定しない。候補選択を求められた場合に限り、判断材料を比較して提示し、決定を待つ。共通表で定める毎release/初回/majorの連続受信は追加soakと区別する。

ユーザーが追加soakを選んだときの実行方法は次の通り。

- 10分/30分: 指定OSで指定profileの連続受信を行い、該当するstatus/card/retune/stop-reopenなどの挙動を記録する。
- 2時間: 指定OSで、選択されたprofileに応じた負荷、反復status/APDU、retune/stop-reopen、FD数・RSSの経時傾向、cleanupを確認する。Q3U4では§0.1の8 receiver T/S混在を使う。
- いずれもshell終了値だけで合否を決めない。TS/counterはSPECの受入条件を使う。FD/RSSは数値固定の合否閾値を設定せず、非安定な持続増加は`判定保留`として調査する。

追加profile認定、single receiver認定、PX-M1UR/PX-S1UR同一serial確認、receiver 7比較の個別条件はSPEC 10.2.6a/10.2.7と各記録を参照する。これらの認定をreleaseごとの代表Linux x86_64 2時間、またはmajor versionごとの各OS/architecture短TSの代わりに使わない。1.0.0初回matrixへの適用は [`release-validation/1.0.0.md`](release-validation/1.0.0.md) に従う。

### PX-M1UR / PX-S1UR の同一serial確認

v0.26 の識別変更が対象のとき、両機種を同時接続できる環境では、§2 の exact candidate を使って `px4d --list-json` を実行し、同じ serial の2筐体が別機種・別USB位置として現れ、双方の `serial_unique` がJSON boolean `false` であることを記録する。通常列挙を許さない Termux では、このコマンドの成功を要求しない。

実施可能なら、同じ状態で `px4d --device 000000000000001 --firmware "$FW" --runtime-dir "$RT"` の曖昧指定が候補と `--usb-path` を示して exit 2 となり、USB interface の claim と endpoint 公開の前に失敗することを確認する。stdout/stderr・終了コードとendpoint残存の有無を保存し、claim前拒否の根拠には模擬USB試験も対応付ける。この同時接続中は、受信・カード・LNB給電・電源制御の試験を行わない。物理USBの抜き差しは利用者の確認を得て行う。実施できない環境では未実施と理由を記録し、順次接続のcanaryや認定結果を同時接続時の識別結果へ流用しない。

### LNB capability表示・15V非対応profileの拒否確認

`lnb_15v_supported`または15V非対応profileの変更時は、exact candidateでM1URとS1URを同時接続し、上の同一serial確認と同じnative列挙環境で`px4d --list-json`を実行する。M1URのISDB-T/S receiverとS1URのISDB-T receiverがJSON boolean `lnb_15v_supported=false`であることを確認する。Q3U4を同時に列挙した場合は、ISDB-T receiverがfalse、ISDB-S receiverがtrueであることも確認し、falseだけを一律に出す誤りを検出する。JSONでは値が文字列ではなくbooleanであることを確認する。Android Termuxは通常のUSB列挙を保証しないため、このlist確認を要求しない。

PX-M1URのISDB-SはLNB 0Vで受信できるが、15V要求は`--allow-lnb-power`の有無によらず`UNSUPPORTED`（exit 3）で拒否され、LNB用GPIOへ書き込まない。LNB/GPIO動作を変更したcandidateでは、PX-M1URだけを接続し、同じS受信要求をdaemonのopt-inなし・ありの両方で試す。daemon起動には毎回X-STARTのコマンド群を使い、なしのrunは通常のX-START、ありのrunは`--allow-lnb-power`を追加して起動し、各run後にdaemonを停止する。各daemon起動後に、次のコマンドを実行してexit 3を記録する:

```sh
if "$D/px4-ts" --device "$ID" --instance "$INSTANCE" --receiver 0 --system isdb-s --freq 1318000000 --slot 0 \
  --runtime-dir "$RT" --lnb-voltage 15 --time 1 --output /dev/null \
  > "$LOG/lnb-15v-rejected.out" 2> "$LOG/lnb-15v-rejected.err"; then rc=0; else rc=$?; fi
echo "$rc" > "$LOG/lnb-15v-rejected.rc"
```

opt-inなし・ありの各runでログ名を分ける。15V拒否時に電圧が出ないことの確認はoffline/mock GPIO試験に結び付け、実機で15Vを許可する試験へ置き換えない。S1URはISDB-T専用なので、その15V確認には使わない。変更が列挙表示だけでLNB/GPIO要求経路に触れていない場合、この物理15V拒否試験は追加せず、表示確認だけを行う。

### USB/cardの物理抜差し

選択された短時間確認では、対象archiveのcandidateに対しB-CASカードの抜去/再挿入とUSB切断/再接続をユーザーが実施する。daemonを起動したままcard hotplugを行い、不在検出と再挿入後のATR/reset/APDU復帰を確認してから、30秒の8 receiver captureでTS/counterを確認する。各物理操作の応答待ち上限は個別に5分とし、要求直前にHAOS/SCS側でCodexは`beep`、Claude Codeは`vibe`を実行する。新しい`tuner-hardware-verified` / `card-core-hardware-verified` claimでは、SPEC 10.3が要求する各抜差しをそのclaimのqualificationで追加実施する。該当matrixで指定された抜差しができなければ、そのartifact/claimは未完了または未認定と記録する。

## 5. 結果の記録と公開可否

v0.1.10のreceiver 7 burstとmacOSの単発CC異常には、SPEC v0.29 §10.5に記録したユーザー決定の
release限定dispositionを適用する。原試行と追加試験を別々に保存し、参照比較の全試行合格や
原因解明、一般的な異常免除を意味する表示に置き換えない。

ハードウェア試験は [`platforms/validation-results.md`](platforms/validation-results.md) に日付付きで追記する。新しい records directory や template framework、汎用スクリプトは作らない。Stable release recordには少なくとも次を残す。

- version、source commit、candidate workflow run、10 archive（9 binary archiveとsource archive）と外側checksumの確認結果、toolchain/build input、static/dynamic link inventory、relink結果、license/corresponding-source条件
- 前回Stable tag、使用したbaseline evidence、baseline以後の累積差分、各artifactのbinary byte-identity判定
- 変更impact分類と hunk-level の call-path / guard 適用条件、対象/除外したmodel-profile・runtime/access path・featureと根拠
- claimごとの `継承` / `今回再検証` / `未認定` / `対象外`、exact candidateでの試験有無
- 適用するmatrixの各binary archiveと対応環境、各実機testの環境ID・host・device/USB ID・runtime/access path・archive SHA-256・日時（UTC）・コマンド・counter・結果・ログ保存先
- soakについてユーザー決定（実施有無・時間・OS）と実施結果。receiver 7 fresh comparison、USB/card抜差しが追加で必要な場合は実施状態と理由
- soakのFD数・RSS記録系列と、`安定` / `頭打ち` / `判定保留` のdispositionおよび理由
- 残存blocker、既知の非blocking制限、README/support表示とrelease noteへの反映

失敗した試行は消さずに記録し、原因を切り分けた後の再試験を別試行として残す。受信設備不備や誤ったコマンド等を特定できても、失敗をpassに書き換えない。

次をすべて満たすまでStable公開へ進まない。

- Stable共通のCI、candidate artifact audit、source/license、checksum gateが成功。
- final candidateについて、該当版の `docs/release-validation/<version>.md` で当該releaseに適用される短時間matrixが成功し、ユーザーがsoakを選んだ場合は指定時間・OSの試験が完了している。soakのFD数・RSSが最終観測まで安定化しない持続的な増加を示し、外部要因を特定できない場合は `判定保留` とし、公開しない。
- READMEの各support claimが実証または適格なbaseline継承に対応し、未試験のmodel × runtime/access path × featureを認定表示していない。
- crash、hang、use-after-free、stale lease、再接続不能、カード経路の重大な未解決issueがない。
- 当該releaseで選択された短時間matrixに未完了の配布artifactがある場合はStable gate未完了とし、そのままsupport表示だけで完了扱いしない。
- SPEC 10.5-2の再現性確認（同一source commitの独立2 runによる10 archive（Windows ZIPを含む）と外側`SHA256SUMS`のbyte一致）を、未実施のままpass扱いしない。toolchain/build inputが異なる比較はinconclusiveとして成功とせず、matching pairを取り直す。正規化比較による合格は認めない。
- README、LICENSE、THIRD_PARTY_NOTICES、provenance、checksum、support表示、release archiveの内容が一致し、公開前レビュー済み。
- 現行 inventory に無い機種は実機未検証としてREADMEに明示すればリリース可能。Betaを機種追加の代わりに使わない。

リリースノートには利用者向け変更、短い検証結果、必要な既知制限だけを書く。試験matrix、内部証拠系譜、詳細log、hash一覧は掲載せず、本記録とREADMEへ分離する。

## 6. 正式アーカイブの環境別手順

各項は §0.1 の共通部品と次の追加手順で構成する。物理操作（USB/card の抜差し、アンテナ、電源、kernel module blacklist変更）はユーザーが事前確認のうえ実施する。HAOS（E01/E02）は production host であり、Supervisor 状態変更は承認・退避・復元を伴う。サービスを再起動しない。

### E01 HAOS x86_64 Debian/glibc SCS（基準環境、container root）

- 前提: `ha core info` で HAOS version と kernel を記録する。container root 実行であることを記録し、非root claim に使わない。`lsusb -t` で kernel px4 driver が掴んでいないことを確認する。`linux-glibc-x86_64` と glibc IFD を使う。
- 手順: §0.1 の X-PREP / X-START / X-LOAD / X-MON / X-STOP / X-RESID。Q3U4 は §0.1 の preflight を先に行う。PC/SC は container の `pcscd` を使い、終了後に元の状態へ戻す。
- 選定: 影響artifactが `linux-glibc-x86_64` のとき、または HAOS container 層が変わったとき。

### E02 HAOS x86_64 Alpine/musl add-on（基準環境、Supervisor）

- 前提: Supervisor add-on の options を GET 相当の現行値で退避し、ユーザー承認のうえ試験用 options を適用して add-on を起動する。終了後に options を復元して add-on を停止する。`ha`/Supervisor の状態変更はこの範囲に限り、HA Core・mirakc を再起動しない。`linux-musl-x86_64` と musl IFD を使う。
- 手順: E01 と同じ。Q3U4 は §0.1 の preflight を先に行う。物理ホットプラグを行う場合はユーザーが実施する。
- 選定: `linux-musl-x86_64`を現行頻度matrixで選択した場合に使用。

### E03 AnduinOS x86_64（基準Linux x86_64）

- 前提: `smsusb`/`smsdvb`/`smsmdtv` を blacklist し、`lsusb -t` で未bindを確認する（siano側の kernel module を掴む場合）。px4 は kernel driver 不要だが、他製品の module 干渉がないことを確認する。udev rule と `video` グループを用意し、非root で実行する。
- 手順: §0.1。Q3U4は§0.1のpreflightを先に行う。`linux-glibc-x86_64` native確認環境例。X-R7REFを行う場合は`tsukumijima/px4_drv`を一時loadし、同一個体・antenna・power・firmware・frequency・durationで取得後unloadする（module unloadもユーザー確認対象）。
- 選定: `linux-glibc-x86_64`を現行頻度matrixで選択した場合に使用。

### E04 macOS arm64（基準darwin環境）

- 前提: `shasum -a 256 -c SHA256SUMS`。`otool -L` で libusb dylib と macOS system 以外の依存がないことを確認する。`sw_vers` を記録する。PC/SC は Homebrew `pcsc-lite` と `ifd/px4-userland-ifd.bundle` を使う。
- 手順: §0.1（`stat -f %z`、`lsof -p`、`ps -o rss=` を用いる）。`darwin-arm64` を使う。
- 選定: `darwin-arm64`を現行頻度matrixで選択した場合に使用。

### E05 Android Termux aarch64（基準android-aarch64、2-FD）

- 前提: Termux と Termux:API（`termux-usb`）、`util-linux`（`setsid`）を用意する。`android-aarch64` の `px4-termux` を使う。native PC/SC adapter は N/A（card は portable IPC 経由）。
- 手順: §0.1 の Termux 2-FD。launcher の SIGINT/SIGTERM/SIGHUP と 40秒猶予、第2 open 失敗、終了後 process/FD/endpoint 残存なしを確認する。
- 選定: `android-aarch64`を現行頻度matrixで選択した場合に使用。

### E06 Android Termux armv7a（基準android-armv7a、2-FD）

- 前提・手順・選定: E05 と同じ。`android-armv7a` を使う。APK は対象外であり、同 device の APK 試験は dtv-android 側で行う。

### E07 Bliss OS x86_64 Termux（基準android-x86_64、2-FD、native確認環境例）

- 前提: 給電中の画面常時点灯と Termux 前面表示を検証中だけ使い、`stay_on_while_plugged_in` を実行前の値へ戻す。`android-x86_64` を使う。
- 手順: E05 と同じ。初回起動の `TIMEOUT` が起きた場合は初回起動の信頼性を未確定として記録し、合格値に埋めない。launcher に渡した device path を記録する。
- 選定: `android-x86_64`を現行頻度matrixで選択した場合に使用。

### E15-G / E15-M Linux aarch64（glibc / musl native確認環境）

- E15-G は `linux-glibc-aarch64` のnative確認環境、E15-M はnative Fedora host上のAlpine arm64/musl runtimeを表す。過去record `df6a1e634e5bec11961a1f0f15eedd1da22f7fee`はbaselineであり、candidateごとの短時間確認を代替しない。
- 手順: 適用matrixに含む際はnative `linux-glibc-aarch64` candidateで列挙・受信・USB抜差し・再接続後の受信を行う。`uname -r`、`getenforce`を記録する。新たなhardware claimの認定はSPEC 10.3に従い別途行い、kernelを記録してclaimをそのkernelに限定する。
- `linux-musl-aarch64`を現行頻度matrixで選択する場合は、同じnative Fedora aarch64 host上のAlpine arm64 Docker containerでcandidate artifactを実行する。CPU emulationを使わず、対象tunerの物理USB deviceをcontainerへ渡す。glibc確認とは別のrunとして記録する。
- 選定: `linux-glibc-aarch64`を現行頻度matrixで選択した場合に使用。

### E17 Windows 11 x86_64（Windowsアーカイブのnative確認・Phase 1/2）

- 位置づけ: `windows-x86_64`配布artifactをmatrixで選択した際のnative回帰試験環境。native libusbのtunerと
  CARD_*、実機の物理回帰を行う。CIのbuild/offline test成功は`build-tested`のみを示し、本節が完了する
  まで当該archiveのgateは未完了、support表示は`未認定`（`hardware-unverified`）である。
  exact candidate archive（`px4-userland-<version>-windows-x86_64.zip`）をnative Windows 11 x64（実機）で
  実行する。`windows-2022` CIはtest OSであり、Windows 11実機の代用にしない。
- 2026-10-12時点の記録にある`未認定`は、その時点でfull E17 gateが未完了だった状態を示す。
  2026-10-13のfrozen v0.2.1 candidateに対するbounded follow-up checksとその限定範囲は
  [validation-results.md](platforms/validation-results.md)を参照する。これは追補 evidenceであり、本節の手順や
  full release gate、将来版の要求項目を置き換えない。
- 対象範囲: `px4d`/`px4-ts`/`px4ctl`とversioned local IPCのCARD_*に加え、SPEC 7.6のx64 B25限定
  application-local WinSCard DLLを確認する。配置・API・対象clientは[Windows WinSCard手順](platforms/windows-winscard.md)に従う。
  一般WinSCard全API、x86/native ARM64 DLL、Microsoft PC/SC IFD登録は対象外。native-card-adapter列は
  未試験なら`未認定`とし、Phase 1の実測やCI成功だけで合格へ変えない。
- preflight（read-only）:
  - `$PSVersionTable.PSVersion`、`[System.Environment]::OSVersion`、`chcp`のdefault code page、`$env:LOCALAPPDATA`を記録する。
  - `$env:LOCALAPPDATA`のowner SID・各ACE・protected flagをread-onlyで記録する。明示的な`--runtime-dir`を使う
    本手順では、この既定profileの環境変数やACLを変更しない。worker wakeとendpointの親検証は明示した`$RT`へ
    適用される。daemon起動前に`$RT`のownerが現在user SIDで、現在userに書込み権があり、書込み可能なACEが
    現在user/SYSTEM/Administratorsだけであることを確認し、owner SID・各ACE・protected flagを記録する。
  - WinUSB backend: `Get-PnpDevice -PresentOnly`と`pnputil /enum-drivers`で対象USB ID（`0511:xxxx`）の現driver
    bindingをinventoryとして記録する。ユーザー確認なしにWinUSB INF・driverのinstall/変更/削除
    （`pnputil /add-driver`・`/delete-driver`等）を行わず、kernel driverやINFを導入しない。
  - PX-S1UD/mirakc等の稼働経路を変更しない。対象Q3U4をclaim中の他daemonがあれば`px4d`が`busy`で失敗することを
    確認する。firmwareは利用者が用意した既存ファイルを`--firmware`で指定するだけで、download・抽出・変換をしない。
- runtime dir / ACL:
  - 明示した`--runtime-dir`はworker wakeにも渡される（SPEC 7.5）。別の`LOCALAPPDATA`へfallbackしない。
    タスク専用の短いlocal drive絶対pathを`$RT`とし、事前に利用者へACL設定を確認してから、ownerを現在user SID、
    継承を切ったprotected DACLで現在userだけに許可して作る。profile ACL・global env・driverは変更しない。
    `verify_runtime_parent`のowner・trusted writer検証、reparse point拒否、file identity検証は弱めない。
    `$RT`配下のworker socketとendpoint socketがともにAF_UNIXの108 byte未満に収まることを確認する。
    長すぎる場合は安全な短い親pathを選び直す。`px4d`は存在しない`--runtime-dir`を拒否するため、`$RT`を事前作成する。
  - setupはWindows PowerShell 5.1（`powershell.exe`）で実行する。PowerShell 7の.NETには
    `[System.IO.Directory]::CreateDirectory(string, DirectorySecurity)`がない。PowerShell 5.1で
    `$ErrorActionPreference = 'Stop'`のままnative commandのstderrを`2>&1`で取り込むと終了errorになるため、
    ready待ちのstatus poll等native commandを呼ぶ範囲は`$ErrorActionPreference`を`Continue`にする。
  - endpointがsame-user private ACLであること、non-ASCII（UTF8）のruntime/firmware/output pathでもargv・
    file openがACPで壊れないことを確認する。
- コマンド（PowerShell。POSIX風の`/dev/null`・`sha256sum`・`$?`をそのまま使わない。空白入りpathに耐えるため
  引数は配列で組み立て、`Start-Process -ArgumentList`の文字列連結は使わない）。`$D`はタスクごとの新しい
  pathを使い、既存treeを`Expand-Archive -Force`で上書きしない。`$LOG`は使用前に作成する。redirectした
  stdout/stderrは必ず`ReadToEndAsync`で drain してから待つ（drainしないとpipeが埋まり`WaitForExit`がhangする）:

  ```powershell
  $ErrorActionPreference = 'Stop'
  $psHomeFull = [IO.Path]::GetFullPath($PSHOME)
  if (-not [Environment]::Is64BitProcess -or
      $PSVersionTable.PSVersion.Major -ne 5 -or $PSVersionTable.PSVersion.Minor -ne 1 -or
      $psHomeFull -notmatch '(?i)\\System32\\WindowsPowerShell\\v1\.0$') {
    throw 'Run this procedure in x64 Windows PowerShell 5.1 from the System32 installation.'
  }
  $childPowerShell = Join-Path $psHomeFull 'powershell.exe'
  $D = 'C:\px4-e17-<taskid>\extract'      # タスク固有の未使用path。既存pathを再利用しない。
  $LOG = 'C:\px4-e17-<taskid>\log'
  $ZIP = 'C:\path\to\px4-userland-<version>-windows-x86_64.zip'
  if (Test-Path $D) { throw "refusing to reuse existing extraction path: $D" }
  New-Item -ItemType Directory -Path $D | Out-Null
  New-Item -ItemType Directory -Path $LOG | Out-Null
  Get-FileHash -Algorithm SHA256 $ZIP
  Expand-Archive -Path $ZIP -DestinationPath $D   # fresh $D への展開。-Force で既存を上書きしない。

  # 既定profileのLOCALAPPDATA owner SID/ACLはread-onlyで記録する（変更しない）。
  $me = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
  $lap = [System.Environment]::GetEnvironmentVariable('LOCALAPPDATA')
  $lapAcl = Get-Acl -LiteralPath $lap
  $lapOwner = $lapAcl.GetOwner([System.Security.Principal.SecurityIdentifier])
  Write-Host "default LOCALAPPDATA=$lap owner=$($lapOwner.Value) currentUser=$($me.Value) protected=$($lapAcl.AreAccessRulesProtected)"
  foreach ($ace in $lapAcl.Access) {
    Write-Host "  lap ace identity=$($ace.IdentityReference) rights=$($ace.FileSystemRights) inherited=$($ace.IsInherited)"
  }

  # 利用者へACL設定を確認したタスク専用の短いruntime parentを使う。LOCALAPPDATAはそのまま継承する。
  $parentRoot = [System.Environment]::GetEnvironmentVariable('RUNNER_TEMP')
  if (-not $parentRoot) { $parentRoot = [System.Environment]::GetEnvironmentVariable('TEMP') }
  $RT = Join-Path $parentRoot ('px4e17-' + [guid]::NewGuid().ToString('N').Substring(0,8))
  if (Test-Path $RT) { throw "refusing to reuse existing runtime parent: $RT" }
  $ID = '<device serial>'   # daemonの--device
  # AF_UNIX sun_pathは108 byte未満。worker socketとendpoint socket（<RT>\px4-userland\<ID>\<name>.sock）の
  # full sampleをUTF-8 byte数で確認する。
  $sampleWorker = Join-Path $RT ('px4-wake-' + ('0' * 16) + '\w.sock')
  $sampleControl = Join-Path (Join-Path (Join-Path $RT 'px4-userland') $ID) 'control.sock'
  $sampleStream = Join-Path (Join-Path (Join-Path $RT 'px4-userland') $ID) 'stream.sock'
  $workerBytes = [System.Text.Encoding]::UTF8.GetByteCount($sampleWorker)
  $controlBytes = [System.Text.Encoding]::UTF8.GetByteCount($sampleControl)
  $streamBytes = [System.Text.Encoding]::UTF8.GetByteCount($sampleStream)
  Write-Host "runtime parent=$RT workerSocketBytes=$workerBytes controlSocketBytes=$controlBytes streamSocketBytes=$streamBytes"
  if ($workerBytes -ge 108 -or $controlBytes -ge 108 -or $streamBytes -ge 108) {
    throw "socket path too long for AF_UNIX: worker=$workerBytes control=$controlBytes stream=$streamBytes"
  }
  # px4dは--runtime-dirの存在を要求する。current user所有・protected DACLで作る。
  $rtAcl = New-Object System.Security.AccessControl.DirectorySecurity
  $rtAcl.SetOwner($me)
  $rtAcl.SetAccessRuleProtection($true, $false)
  $rtAcl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule(
    $me, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')))
  [System.IO.Directory]::CreateDirectory($RT, $rtAcl) | Out-Null
  $actualRtAcl = Get-Acl -LiteralPath $RT
  $rtOwner = $actualRtAcl.GetOwner([System.Security.Principal.SecurityIdentifier])
  Write-Host "runtime parent=$RT owner=$($rtOwner.Value) currentUser=$($me.Value) protected=$($actualRtAcl.AreAccessRulesProtected)"
  foreach ($ace in $actualRtAcl.Access) {
    Write-Host "  rt ace identity=$($ace.IdentityReference) rights=$($ace.FileSystemRights) inherited=$($ace.IsInherited)"
  }

  # .NET Framework (PowerShell 5.1) ProcessStartInfo.Arguments用のWindows引用。
  function ConvertTo-WindowsArg([string]$value) {
    if ($value -notmatch '[ \t"]') { return $value }
    $escaped = [regex]::Replace($value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
  }
  function Join-WindowsArgs([string[]]$values) {
    return ($values | ForEach-Object { ConvertTo-WindowsArg $_ }) -join ' '
  }
  function Start-OwnedProcess([string]$file, [string[]]$arguments, [bool]$stdin) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $file
    $psi.Arguments = Join-WindowsArgs $arguments
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $stdin
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    return [pscustomobject]@{
      Process = $proc
      StdoutTask = $proc.StandardOutput.ReadToEndAsync()
      StderrTask = $proc.StandardError.ReadToEndAsync()
    }
  }
  function Stop-OwnedProcess($owned, [int]$boundMs, [string]$name) {
    if (-not $owned.Process.HasExited) { $owned.Process.StandardInput.Close() }
    $exited = $owned.Process.WaitForExit($boundMs)
    if (-not $exited) {
      # 未完processのdrain taskへResultで入らない。PID/timeout evidenceを保存してthrowする。
      $owned.Process.Id | Out-File (Join-Path $LOG "$name.timeout.txt")
      throw "owned $name $($owned.Process.Id) did not exit within ${boundMs}ms; do not force-kill without explicit failure-recovery authorization"
    }
    $stdoutDrained = $owned.StdoutTask.Wait(5000)
    $stderrDrained = $owned.StderrTask.Wait(5000)
    if (-not ($stdoutDrained -and $stderrDrained)) {
      $owned.Process.Id | Out-File (Join-Path $LOG "$name.drain-timeout.txt")
      throw "owned $name $($owned.Process.Id) exited but its output drains did not complete; evidence in $LOG"
    }
    $owned.StdoutTask.Result | Out-File (Join-Path $LOG "$name.out")
    $owned.StderrTask.Result | Out-File (Join-Path $LOG "$name.err")
    "$($owned.Process.ExitCode)" | Out-File (Join-Path $LOG "$name.rc")
    return $owned.Process.ExitCode
  }
  function Start-Ts([int]$receiver, [string]$system, [long]$frequency, [string]$slot) {
    $a = @('--device', $ID, '--receiver', "$receiver", '--system', $system, '--freq', "$frequency")
    if ($slot) { $a += @('--slot', $slot) }
    $a += @('--runtime-dir', $RT, '--output', 'NUL', '--time', '30')
    return Start-OwnedProcess (Join-Path $D 'px4-ts.exe') $a $false
  }
  function Wait-Captures($captures) {
    $timeout = @()
    foreach ($c in $captures) {
      if (-not $c.Process.WaitForExit(90000)) {   # 30s tune + 30s capture + cleanup margin
        # 未完processのdrain taskへResultで入らない。timeoutを記録し他captureの判定は継続する。
        "$($c.Process.Id)" | Out-File -Append "$LOG\ts-timeout.txt"
        $timeout += $c.Process.Id
        continue
      }
      $stdoutDrained = $c.StdoutTask.Wait(5000)
      $stderrDrained = $c.StderrTask.Wait(5000)
      if (-not ($stdoutDrained -and $stderrDrained)) {
        "$($c.Process.Id) drain incomplete" | Out-File -Append "$LOG\ts-timeout.txt"
        $timeout += $c.Process.Id
        continue
      }
      $c.StderrTask.Result | Out-File -Append "$LOG\ts.err"
      "$($c.Process.ExitCode)" | Out-File -Append "$LOG\ts.rc"
    }
    if ($timeout.Count -ne 0) {
      throw "TS capture(s) exceeded the 90s bound without Kill; evidence in $LOG\ts-timeout.txt: $($timeout -join ', ')"
    }
  }
  & "$D\px4d.exe" --list-json | Out-File "$LOG\discovery.json"
  ```

  daemon起動・ready確認・受信・CARD・停止（X-START/X-LOAD/X-MON/X-CARD-HP/X-STOP相当）。`$ID`は`--device`、
  `$FW`は利用者提供firmware。daemonとCLIは同じ明示的な`--runtime-dir $RT`を使い、`LOCALAPPDATA`はそのまま継承する。
  標準入力をredirectし、`--exit-on-stdin-eof`でcooperative shutdownする。
  ready判定はexit codeだけでなくstatusの`ready=yes` fieldを確認する。CARDは**daemon停止前**に実行する。
  受信はQ3U4では8 receiverを**同時に**起動し（逐次実行で代用しない）、各processを有限時間で待つ。他profileは
  SPEC 10.2.7の該当receiverだけを使う。コマンド失敗時も`finally`でowned stdinをcloseしてcleanupを試み、
  主失敗を隠さない:

  ```powershell
  $owned = Start-OwnedProcess (Join-Path $D 'px4d.exe') `
    @('--device', $ID, '--firmware', $FW, '--runtime-dir', $RT, '--exit-on-stdin-eof') $true
  $primary = $null
  try {
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) {
      $status = (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT status 2>> "$LOG\px4ctl.err") | Out-String
      $status | Out-File -Append "$LOG\status-start.txt"
      if ($LASTEXITCODE -eq 0 -and $status -match 'ready=yes') { $ready = $true; break }
      Start-Sleep -Seconds 1
    }
    if (-not $ready) { throw 'px4d did not reach ready=yes within the bounded poll' }

    # 独立したx64 Windows PowerShell 5.1子processでWinSCard smoke。child sourceはPhase 2欄のコードを事前保存する。
    $probePath = Join-Path $LOG 'winscard-smoke.ps1'
    if (-not (Test-Path -LiteralPath $probePath -PathType Leaf)) { throw "missing WinSCard smoke script: $probePath" }
    $probe = Start-OwnedProcess $childPowerShell @(
      '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $probePath,
      '-DllPath', (Join-Path $D 'winscard\WinSCard.dll'), '-RuntimeDir', $RT, '-DeviceSerial', $ID
    ) $true
    $probeExit = Stop-OwnedProcess $probe 30000 'winscard-smoke'
    if ($probeExit -ne 0) { throw "WinSCard smoke child exit code was $probeExit" }

    $captures = @()
    # Q3U4: 0/1/4/5=ISDB-S、2/3/6/7=ISDB-T。8 receiverを同時起動する。他profileはSPEC 10.2.7の該当のみ。
    foreach ($r in 0..7) {
      if ($r -in 0,1,4,5) { $captures += Start-Ts $r 'isdb-s' 1318000000 '0' }
      else { $captures += Start-Ts $r 'isdb-t' 527143000 '' }
    }
    Wait-Captures $captures
    (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT status 2>> "$LOG\px4ctl.err") | Out-File "$LOG\status-after.txt"

    # CARD（daemon停止前）。抜去/再挿入の物理操作を挟み、各出力とexit値を保存する。
    foreach ($op in 'card-status','card-atr','card-reset') {
      (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT $op 2>> "$LOG\card.err") | Out-File "$LOG\$op.txt"
      "$LASTEXITCODE" | Out-File "$LOG\$op.rc"
    }
    1..10 | ForEach-Object {
      (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT card-apdu 90:30:00:00:00 2>> "$LOG\card.err") | Out-File "$LOG\card-$_.apdu"
      "$LASTEXITCODE" | Out-File "$LOG\card-$_.rc"
    }
  } catch {
    $primary = $_
  } finally {
    try { Stop-OwnedProcess $owned 35000 'px4d' | Out-Null }
    catch { if ($null -eq $primary) { $primary = $_ } }
  }
  if ($null -ne $primary) { throw $primary }
  ```

  停止後の確認は所有PIDだけを見る（無関係なglobal processを混ぜない）。実際のexit codeとendpoint残留を
  確認し、空のowned runtime dirだけを削除する。既存の`$parentRoot`は削除しない:

  ```powershell
  if (-not $owned.Process.HasExited) { throw 'owned px4d is still running after the shutdown attempt' }
  if ($owned.Process.ExitCode -ne 0) { throw "owned px4d exit code was $($owned.Process.ExitCode)" }
  $ownedDirs = @($RT)
  foreach ($dir in $ownedDirs) {
    if (-not (Test-Path $dir)) { continue }
    $left = Get-ChildItem -Force $dir
    if ($left) { throw "runtime dir still has entries after cleanup: $dir => $($left.Name -join ', ')" }
    Remove-Item $dir -Force   # 空のowned dirだけを削除する。失敗証拠を隠す再帰削除は行わない。
  }
  ```

  CARDの合否は`card-status`の`present=no`、`card-atr`/`card-apdu`の`NO_CARD`（exit 9）、再挿入後の
  ATR/reset/APDU 10回成功をoutput fieldとexit値で確認する。これは直接CARD_*の証拠であり、次のWinSCard試験を代用しない。

- Phase 2実機への引継ぎ（SPEC 7.6 / [Windows WinSCard手順](platforms/windows-winscard.md)）:
  - 以下のchild scriptを先に$LOG\winscard-smoke.ps1へ保存する。上のE17 X-START blockがready=yesを確認した直後に
    起動し、Stop-OwnedProcessがstdin EOFを送り、30秒でchild終了を待ち、stdout/stderrを各5秒drainして
    winscard-smoke.out/.err/.rcへ保存する。返却exit codeが非0なら失敗とするため、parent daemonはreadyのまま、
    daemon shutdown前にsmokeが終わる。child起動時の-ExecutionPolicy Bypassはそのprocessだけに適用し、
    user/machine policyを永続変更しない。上のpreflightでparentがx64 Windows PowerShell 5.1であることも確認する。
    DLL pathはcandidate展開先の$D\winscard\WinSCard.dll、device/runtimeは既存の$ID/$RTを使う。
    child processだけにPX4_WINSCARD_RUNTIME_DIR=$RTを設定する。ambient LOCALAPPDATAは変更しない。
    PowerShellのmanaged呼出しでは$nullがC#のstringに渡る時に空文字へ変換される（IsNull($null)=false、Length($null)=0）。
    そのためstring groupsに$nullを渡すと実NULL pointerにならず、今回の初回smokeはcandidateがNULL以外のgroupを拒否して
    0x80100022で失敗した。P/Invoke宣言をIntPtr groupsとし、queryとbuffer両方へ[IntPtr]::Zeroを渡す。
    下記のCompileOnly/SelfTestはAPIを呼ばない静的確認用。
    ~~~powershell
    param(
      [Parameter(Mandatory=$true)][string]$DllPath,
      [Parameter(Mandatory=$true)][string]$RuntimeDir,
      [Parameter(Mandatory=$true)][string]$DeviceSerial,
      [switch]$CompileOnly,
      [switch]$SelfTest
    )
    $ErrorActionPreference = 'Stop'
    if (-not [Environment]::Is64BitProcess) { throw 'Use x64 Windows PowerShell.' }
    if ($PSVersionTable.PSVersion.Major -ne 5 -or $PSVersionTable.PSVersion.Minor -ne 1) {
      throw 'Use Windows PowerShell 5.1.'
    }
    $DllPath = [IO.Path]::GetFullPath($DllPath)
    $RuntimeDir = [IO.Path]::GetFullPath($RuntimeDir)
    if (-not (Test-Path -LiteralPath $DllPath -PathType Leaf)) { throw 'Candidate WinSCard.dll not found.' }
    [Environment]::SetEnvironmentVariable('PX4_WINSCARD_RUNTIME_DIR', $RuntimeDir, 'Process')
    $dllLiteral = $DllPath.Replace('\', '\\').Replace('"', '\"')
    $nativeSource = @"
    using System;
    using System.Collections.Generic;
    using System.Runtime.InteropServices;
    using System.Text;
    public static class Px4WinSCardSmoke {
      public static bool IsNull(string value) { return value == null; }
      public static int StringLength(string value) { return value == null ? -1 : value.Length; }
      public static bool IsZero(IntPtr value) { return value == IntPtr.Zero; }
      [DllImport("$dllLiteral", EntryPoint="SCardEstablishContext", ExactSpelling=true)]
      public static extern int Establish(uint scope, IntPtr r1, IntPtr r2, out IntPtr context);
      [DllImport("$dllLiteral", EntryPoint="SCardReleaseContext", ExactSpelling=true)]
      public static extern int Release(IntPtr context);
      [DllImport("$dllLiteral", EntryPoint="SCardListReadersW", CharSet=CharSet.Unicode, ExactSpelling=true)]
      public static extern int ListReaders(IntPtr context, IntPtr groups, IntPtr readers, ref uint length);
      [DllImport("$dllLiteral", EntryPoint="SCardConnectW", CharSet=CharSet.Unicode, ExactSpelling=true)]
      public static extern int Connect(IntPtr context, string reader, uint share, uint protocols,
        out IntPtr card, out uint activeProtocol);
      [DllImport("$dllLiteral", EntryPoint="SCardStatusW", CharSet=CharSet.Unicode, ExactSpelling=true)]
      public static extern int Status(IntPtr card, StringBuilder reader, ref uint readerLength,
        out uint state, out uint protocol, [Out] byte[] atr, ref uint atrLength);
      [DllImport("$dllLiteral", EntryPoint="SCardDisconnect", ExactSpelling=true)]
      public static extern int Disconnect(IntPtr card, uint disposition);
      [DllImport("kernel32.dll", EntryPoint="GetModuleHandleW", CharSet=CharSet.Unicode,
        ExactSpelling=true, SetLastError=true)]
      public static extern IntPtr GetModuleHandle(string absolutePath);
      [DllImport("kernel32.dll", EntryPoint="GetModuleFileNameW", CharSet=CharSet.Unicode,
        ExactSpelling=true, SetLastError=true)]
      public static extern uint GetModuleFileName(IntPtr module, StringBuilder path, uint capacity);
      public static string[] ReadMultiSz(IntPtr buffer, uint length) {
        var values = new List<string>();
        var current = new StringBuilder();
        bool previousWasNull = false;
        for (uint i = 0; i < length; i++) {
          char c = (char)Marshal.ReadInt16(buffer, checked((int)(i * 2)));
          if (c == '\0') {
            if (current.Length == 0) {
              if (previousWasNull || values.Count == 0) break;
              previousWasNull = true;
            } else {
              values.Add(current.ToString());
              current.Length = 0;
              previousWasNull = true;
            }
          } else {
            current.Append(c);
            previousWasNull = false;
          }
        }
        return values.ToArray();
      }
    }
    "@
    Add-Type -TypeDefinition $nativeSource -Language CSharp
    function Format-ScardCode([int]$Code) {
      $raw = [BitConverter]::GetBytes([int]$Code)
      $unsigned = [BitConverter]::ToUInt32($raw, 0)
      return ('0x{0:X8}' -f $unsigned)
    }
    if ($CompileOnly) { Write-Output 'COMPILE_ONLY=PASS; no WinSCard API called.'; exit 0 }
    if ($SelfTest) {
      $test = [Runtime.InteropServices.Marshal]::StringToHGlobalUni(("alpha" + [char]0 + "beta" + [char]0))
      try {
        [string[]]$parsed = @([Px4WinSCardSmoke]::ReadMultiSz($test, 12))
        if ($parsed.Count -ne 2 -or $parsed[0] -cne 'alpha' -or $parsed[1] -cne 'beta') {
          throw 'MULTI_SZ self-test failed.'
        }
      } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($test) }
      if ([Px4WinSCardSmoke]::IsNull($null) -or [Px4WinSCardSmoke]::StringLength($null) -ne 0) {
        throw 'PowerShell managed null conversion self-test failed.'
      }
      if (-not [Px4WinSCardSmoke]::IsZero([IntPtr]::Zero) -or
          [Px4WinSCardSmoke].GetMethod('ListReaders').GetParameters()[1].ParameterType -ne [IntPtr]) {
        throw 'ListReaders NULL-pointer ABI self-test failed.'
      }
      if ((Format-ScardCode 0) -cne '0x00000000' -or
          (Format-ScardCode -2146435038) -cne '0x80100022' -or
          (Format-ScardCode -2146435064) -cne '0x80100008') {
        throw 'SCARD return-code formatting self-test failed.'
      }
      Write-Output 'SELFTEST=PASS; MULTI_SZ, NULL-pointer ABI, and SCARD return-code formatting; no WinSCard API called.'
      exit 0
    }

    $context = [IntPtr]::Zero
    $card = [IntPtr]::Zero
    $failed = $false
    try {
      $rc = [Px4WinSCardSmoke]::Establish(0, [IntPtr]::Zero, [IntPtr]::Zero, [ref]$context)
      Write-Output ('SCardEstablishContext=' + (Format-ScardCode $rc))
      if ($rc -ne 0) { throw 'SCardEstablishContext failed.' }
      $module = [Px4WinSCardSmoke]::GetModuleHandle($DllPath)
      if ($module -eq [IntPtr]::Zero) { throw 'GetModuleHandleW did not find the candidate module.' }
      $modulePath = New-Object Text.StringBuilder 32768
      $moduleLength = [Px4WinSCardSmoke]::GetModuleFileName($module, $modulePath, [uint32]$modulePath.Capacity)
      if ($moduleLength -eq 0 -or $moduleLength -ge $modulePath.Capacity) { throw 'GetModuleFileNameW failed or truncated.' }
      $loaded = [IO.Path]::GetFullPath($modulePath.ToString()).TrimEnd('\')
      Write-Output ('LOADED_MODULE_PATH=' + $loaded)
      if (-not [string]::Equals($loaded, $DllPath.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Loaded module is not the candidate absolute DLL path.'
      }

      [uint32]$length = 0
      $rc = [Px4WinSCardSmoke]::ListReaders($context, [IntPtr]::Zero, [IntPtr]::Zero, [ref]$length)
      Write-Output ('SCardListReadersW(query)=' + (Format-ScardCode $rc) + '; chars=' + $length)
      if ($rc -notin @(0, [int]0x80100008) -or $length -lt 2 -or $length -gt 65536) {
        throw 'SCardListReadersW size query failed or returned an invalid MULTI_SZ length.'
      }
      $allocated = $length
      $buffer = [Runtime.InteropServices.Marshal]::AllocHGlobal([int]$length * 2)
      try {
        $rc = [Px4WinSCardSmoke]::ListReaders($context, [IntPtr]::Zero, $buffer, [ref]$length)
        Write-Output ('SCardListReadersW(buffer)=' + (Format-ScardCode $rc) + '; chars=' + $length)
        if ($rc -ne 0 -or $length -gt $allocated) { throw 'SCardListReadersW buffer call failed.' }
        [string[]]$readers = @([Px4WinSCardSmoke]::ReadMultiSz($buffer, $length))
      } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($buffer) }

      $target = 'PX4 userland (' + $DeviceSerial + ')'
      $matches = @($readers | Where-Object { [string]::Equals($_, $target, [StringComparison]::Ordinal) })
      Write-Output ('READER_COUNT=' + $readers.Count + '; TARGET_READER_MATCHES=' + $matches.Count)
      if ($matches.Count -ne 1) { throw 'Expected exactly one reader for the selected device.' }
      [uint32]$active = 0
      $rc = [Px4WinSCardSmoke]::Connect($context, $target, 2, 2, [ref]$card, [ref]$active)
      Write-Output ('SCardConnectW=' + (Format-ScardCode $rc) + '; active_protocol=' + $active)
      if ($rc -ne 0 -or $active -ne 2 -or $card -eq [IntPtr]::Zero) {
        throw 'SCardConnectW did not establish SHARED/T=1.'
      }

      $readerName = New-Object Text.StringBuilder 1024
      [uint32]$readerLength = [uint32]$readerName.Capacity
      [uint32]$state = 0
      [uint32]$protocol = 0
      [uint32]$atrLength = 64
      $atr = New-Object byte[] 64
      $rc = [Px4WinSCardSmoke]::Status($card, $readerName, [ref]$readerLength,
        [ref]$state, [ref]$protocol, $atr, [ref]$atrLength)
      Write-Output ('SCardStatusW=' + (Format-ScardCode $rc) + '; state=' + $state +
        '; protocol=' + $protocol + '; ATR_length=' + $atrLength)
      if ($rc -ne 0 -or $state -ne 6 -or $protocol -ne 2 -or
          $readerLength -gt $readerName.Capacity -or $atrLength -gt $atr.Length -or
          -not [string]::Equals($readerName.ToString(), $target, [StringComparison]::Ordinal)) {
        throw 'SCardStatusW returned an invalid state, protocol, or output length.'
      }
      # ATR bytes and card identity are intentionally never printed or saved.
    } catch {
      Write-Error -ErrorAction Continue $_
      $failed = $true
    } finally {
      if ($card -ne [IntPtr]::Zero) {
        try {
          $rc = [Px4WinSCardSmoke]::Disconnect($card, 0) # SCARD_LEAVE_CARD
          Write-Output ('SCardDisconnectLEAVE=' + (Format-ScardCode $rc))
          if ($rc -ne 0) { $failed = $true }
        } catch { Write-Error -ErrorAction Continue $_; $failed = $true }
      }
      if ($context -ne [IntPtr]::Zero) {
        try {
          $rc = [Px4WinSCardSmoke]::Release($context)
          Write-Output ('SCardReleaseContext=' + (Format-ScardCode $rc))
          if ($rc -ne 0) { $failed = $true }
        } catch { Write-Error -ErrorAction Continue $_; $failed = $true }
      }
    }
    if ($failed) { exit 1 }
    exit 0
    ~~~
  - exact candidate ZIP/run/SHA-256と同版daemon/DLLを使う。applicationの起動前に、そのprocessの
    `PX4_WINSCARD_RUNTIME_DIR`へ`$RT`を渡す。ambient `LOCALAPPDATA`は変更しない。EXE横への配置だけで成功とせず、
    applicationが実際にロードしたWinSCard module pathを確認する。System32/SysWOW64・KnownDLL・service設定を変更しない。
  - EDCB＋選択したB25Decoder実装、recisdb-rs＋そのlibaribb25、TVTest＋logue/CasProcessor＋logue/TvCas
    （Release x64、Unicode、B25.tvcas、SmartCard選択）を別々のchainとして記録する。上記手順のsource参照を基準に、
    実際に使った各EXE/decoder/plugin/DLLのsource revision、version、bitness、SHA-256とloaded module pathを残す。
    source不明や相違は明記し、別version・任意pluginへ互換性を拡張しない。
  - reader表示・起動成功・ATR/APDUと、実TSのB25解除結果を別項目にする。実際に解除されたTS/視聴・録画の証拠、
    使用条件・counter・logを保存する。起動や直接IPC APDUだけの成功をdecoder互換性の合格にしない。
  - 内蔵cardの初期化、正常終了、初期化失敗時RESET、外部reader併用とOS providerの実module path、application再起動を確認する。
    ユーザー確認後のcard抜去をdaemonに観測させ、再挿入後も旧handleがstaleで暗黙APDU/再接続されないこと、
    明示的な切断→新規接続で復旧することを確認する（DLLは`SCardReconnect`を公開しない）。USB切断・再接続後は
    daemonを正常停止して再起動し、旧接続を自動復活させずapplication側も明示的に再初期化して復旧を確認する。
  - exact TvCas chainではstalled daemon下の終了・再起動も記録する。各CARD requestの4秒timeout、host側retryの累積、
    起動20秒/終了5秒の期限と実際の終了挙動を確認し、通常終了やmock成功を`TerminateThread`時の安全性へ拡張しない。
    hangや未回収resourceは失敗として残し、無許可のforce-killで合格へ変えない。
  - 未実施・失敗・判定保留を残し、上記の新規仕様試験、既存9 artifact matrix、再現性確認、ユーザーのsoak判断と
    必要な試験が揃うまでgateを完了にしない。既存support claimや過去版だけのリスク受入を今回へ自動継承しない。

- USB再接続（X-USB-HP相当）: 1 receiver clientを起動し、ユーザーの物理USB切断で`DISCONNECTED`/exit 7と
  client残留なしを確認する。再接続後は`px4d --list-json`で再列挙を確認し、旧daemonはX-STOPと同じ
  `Stop-OwnedProcess $owned 35000 'px4d'`でcooperative stopしてから、同じ手順で起動し直す。readyを再確認し、
  短い受信とCARD APDUを再実行する。旧daemonの自動復帰を合格条件にしない。
- 物理操作とsoak: B-CAS/USBの物理操作はユーザーが行い、各操作を依頼する直前にHAOS/SCS側でCodexは`beep`、
  Claude Codeは`vibe`を実行し、依頼から最大5分待つ。LNBは0Vのままとし、別途許可された隔離測定なしに15Vを
  要求・測定しない。5分を超える連続負荷試験は本節に含めず、SPEC 10.5.2「Soak: user decision」に従って
  短時間確認の完了後にユーザーが有無・時間・対象OSを決める。
- 適用条件: Q3U4の項目（8 receiver同時captureを含む）は実際に接続した機種がQ3U4の場合だけ適用し、他profileは
  SPEC 10.2.7の該当項目を使う。TS/counterの合否はSPEC 10.2の適用条件で判定し、shell終了値だけで決めない。
  新しい汎用検証scriptやframeworkは追加しない。
