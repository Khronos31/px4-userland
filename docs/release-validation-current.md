# 現行 Stable 候補のリリース前回帰手順

この文書は現行配布構成に対する候補ごとの手順です。過去候補の行列・soak時間・実測値は今回候補の合格を意味しません。公開契約は [`SPEC.md`](../SPEC.md)、CLIの引数は [`README.md`](../README.md) と各コマンドの `--help` が正です。既存の詳細な候補記録は [`release-validation.md`](release-validation.md) を参照してください。

## 1. 候補と配布物

候補source commitを固定し、そのcommitの `build_userland.yml` candidate artifactを使う。CI run/ref/SHA、artifact ID、各配布archiveのSHA-256、source archive、manifest、ライセンス監査結果を保存する。配布対象は Linux glibc/musl x86_64/aarch64、macOS arm64、Android aarch64/armv7a/x86_64、Windows x86_64 の9 binary archiveとsource archive。候補archive以外のローカルbuildを実機結果として扱わない。

CI artifactを新規空ディレクトリへ展開し、archive内の `SHA256SUMS` を確認する。

```sh
mkdir candidate && cd candidate
tar -xf <candidate-artifact.tar.gz>
sha256sum -c SHA256SUMS
```

Windows ZIPはPowerShellの `Expand-Archive` と `Get-FileHash -Algorithm SHA256` で同様に照合する。全9 binaryとsourceの欠落、余分なファイル、license/provenance不一致は失敗として候補を止める。

## 2. device-free gate

候補commitでworkflowの全jobが成功したことを確認する。source archiveを新規ディレクトリへ展開し、README記載のOS別build/test/package/auditコマンドを実行する。Linux/macOSでは `scripts/build-linux-static.sh --output build-linux-static`、`scripts/build-linux-ifd.sh --libc glibc --output build-linux-ifd-glibc`、`scripts/build-linux-ifd.sh --libc musl --output build-linux-ifd-musl`、Windowsでは `scripts/build-windows.sh --output build-windows` を該当ターゲットで使う。Androidは各ABIのREADME記載NDK buildを使う。全出力・終了値を保存し、source archiveからネットワークなしで再構成できること、binary auditとlicense一覧が成功することを確認する。対象外ターゲットのbuildはその理由を記録し、配布binaryの試験完了と混同しない。

各アーカイブで `px4d --version`、`px4-ts --version`、`px4ctl --version`、`px4d --models` と `px4d --list-json` を実行する。前者は候補版とmodel一覧、後者はUSB有無にかかわらず妥当なJSON (`schema`、`producer`、`scan`) を返すこと。未知引数はexit 2。機器アクセスがない `--list-json` でopen/claimしない。

## 3. 環境と受信機確認

各archiveはnative OS/architectureで展開して起動する。最低時間はLinux x86_64代表機2時間、macOS arm64 30分、Windows x86_64 30分、その他の全配布binary各10分。2時間は製品の各機種が一度以上、かつ各releaseの代表Linux x86_64機で実施する。releaseごとの全binaryはそれぞれの実機・代表/影響機種で短時間回帰する。major versionでは各OS/architectureの短時間TSも行う。条件を別archiveや別OSの結果で代用しない。

環境ごとに候補SHA/archive SHA、OS/arch、model/serial（公開記録では秘匿可）、USB selector、firmware、RF/antenna、電源、実行時刻、実コマンド、終了値、TS/counterを記録する。代表機は接続前に `px4d --list-json` でproducer inventoryを採り、実際に見えたmodel/device/receiverと選択値を対応付ける。USB位置は候補ごとの実値へ置き換える。

daemonとcaptureの実行はarchive READMEの起動形を使う。例（Linux/macOS、値は当該環境の実測値に置換）:

```sh
RT=$(mktemp -d "${TMPDIR:-/tmp}/px4-release.XXXXXX")
px4d --device usb:<BUS-PORT> --firmware <FIRMWARE> --runtime-dir "$RT" & P=$!
px4ctl --device usb:<BUS-PORT> --runtime-dir "$RT" status
px4ctl --device usb:<BUS-PORT> --runtime-dir "$RT" list
px4-ts --device usb:<BUS-PORT> --receiver <ID> --channel T27 --runtime-dir "$RT" --output t27.ts --time 10
```

`status` がready、`list` が対象receiverを示すことを確認する。T27を10秒受信した出力は非空の188-byte packet列で同期し、daemonのpacket/sync/TEI/continuity/queue/USB統計と終了値を記録する。TS品質はSPEC 10の条件で判定し、単なる非空出力を合格根拠にしない。異常ならTS・stderr・counterを保存し、再取得で失敗を上書きしない。

地上波の実地域全チャンネルをそれぞれ10秒受信し、無料衛星の全対象チャンネルも各10秒受信する。チャンネル名・周波数・TSID・slotは現地の受信計画と機器のチューナー表から実値を列挙し、試験記録に固定する。CSを含む無料波のraw TSを除外しない。全8 receiverを公開仕様上有するQ3U4では各receiverを個別にも短時間取得し、SPEC記載のreceiver別条件を確認する。地上波/衛星を持たないprofileは対象外を明記する。

正常停止後に同じruntimeで再取得し、終了値0、daemonの正常cleanup、再取得成功を確認する。`px4ctl card-status`, `card-atr`, `card-reset`, `card-apdu 90:30:00:00:00 --repeat 10` をカード挿入時に実行する。B-CASを抜く前にcaptureを止め、ユーザーが抜去後にstatusが不在、ATR/APDUがNO_CARD/exit 9となることを確認し、再挿入後present、ATR/reset/APDU反復が成功することを確認する。生ATR/APDU・カード識別値は公開記録へ保存しない。Linux/macOSのPC/SC経路とWindows WinSCard経路を分けて確認し、カード再挿入後に新規captureも行う。

## 4. USB・LNB

daemon readyのidle状態とcapture中の各々で、ユーザーが対象USBを抜く。受信中clientはUSB切断としてexit 7を期待する。旧daemonの自動復旧は前提にせず、通常の停止・cleanup、同じ物理portでの再列挙、daemonの新規起動、status/list、T27再取得の順に確認する。戻したdeviceがBUSYなら奪わず所有者と理由を記録する。AndroidはTermux USB許可/FD launcher、WindowsはWinUSBと対象driverを事前確認する。

LNB試験は `--allow-lnb-power` を実装する対応profileでだけ行う。機種のLNB能力をREADME/SPECで確認し、壁アンテナ・他の給電機器を切り離し、衛星端子にテスターを接続してから実施する。受信時の `--lnb-voltage 0` と `15` は独立LNB制御とは別項目で、電圧要求と実測を記録する。対応時は `px4ctl ... lnb-on` 後に実電圧15V、`lnb-off` 後に電源要求0を確認する。非対応機種では要求しない。最後にdaemonを正常停止し、実測0Vと元の配線復帰を確認する。software statusだけで電圧を合格扱いしない。

## 5. soak・片付け・リリース判定

各時間を満たす連続受信では同じ条件でchannelを維持し、受信機全体の状態・TSカウンタ・process/FD/RSSを開始・定期・終了時に保存する。地域全chおよび無料衛星全対象の10秒試験はsoakとは別に行う。候補archive、時間、OS/arch、機種、channel、終端理由を記録する。時間未達・切断・TS不整合は失敗/未完として残す。

daemon/clientを通常停止し、status、process残留、runtime socketの後始末を確認する。daemonが残る場合はログと状態を保存してから対象PIDだけを終了する。記録にはcandidate/ref/run/artifact/hash、実機環境、コマンド、人的操作、期待値と実測、全失敗、未試験/対象外、cleanup、判定を含める。全gate後にだけ結果文書をcommitし、source/build inputを変えていないことを確認する。この文書は検証手順でありtag/release実行の許可ではない。

## 6. 製品固有の適用範囲

PX4の内蔵CARDとLNBは機種ごとに機能差がある。receiver ID、放送方式、LNB 15V対応、CARD reader数は [`README.md`](../README.md) と [`SPEC.md`](../SPEC.md) を参照し、存在しない機能の試験を要求しない。PIC/Emergency等の別ハード仕様を一般化せず、対象機種ごとに状態を `今回再検証` / `継承` / `未認定` / `対象外` と記す。既知Issueや過去候補の結果を今回candidateのpassへ読み替えない。
