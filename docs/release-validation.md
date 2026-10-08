# Stable リリース前検証手順

この文書は Stable リリースごとの実行手順を定める。合否条件と認定条件の正本は [`SPEC.md`](../SPEC.md) 10章であり、両者に差があれば SPEC を優先し、手順書を更新する。

Stable releaseは次の順で進める。

1. リリース候補のsource commitを固定し、そのcommitからCIでcandidate artifactを作る。
2. candidate artifactを展開し、必要な全OS/architectureで実機試験と仕様変更の試験を完了する。
3. 試験結果を文書に記録してcommitする。
4. その結果commitにタグを付け、release CIを実行して公開する。

工程2の全試験が完了するまではcandidate commitを固定して使う。工程2の完了後、工程3で試験結果を記録するcommitは次工程への移行であり、candidateの差し替えではない。工程3の結果commit後に工程2へ戻って実機試験を繰り返さない。candidateのsource、package、build inputを工程2の完了前に変更した場合は、その変更を含む新candidateをCIで作り、影響する試験を行う。

配布する各主要OS/architecture binary artifactについて、工程2でfinal candidateの実機確認を行う。必須検証の一連の操作に総時間上限は設けない。物理抜差しをユーザーに依頼した時点から、その操作への応答を最大5分待つ。各依頼の直前にHAOS側でCodexは`beep`、Claude Codeは`vibe`を実行する。5分を超える連続負荷試験は別のsoak手順にする。安定性に影響し得る変更のsoak有無、時間（10分/30分/2時間）、対象OSはユーザーが決める。エージェントは変更差分・過去記録・影響し得る範囲を要約し、判断を待つ。検証状態の語彙は `継承` / `今回再検証` / `未認定` / `対象外` に統一する。

## 0. 配布artifact matrixと手順の共通部品

短時間確認は以下のartifactごとに実施する。soak対象OSはユーザーが決める。AppArmor、usbip、SELinux などの変化は独立した環境IDにせず、その環境のaccess pathとして記録する。各環境のproduct-specificな準備と物理操作は§6に示す。

| ID | 環境 | px4 の位置づけ | 主 artifact / doc |
|---|---|---|---|
| E01 | HAOS x86_64 Debian/glibc SCS | canonical runtime（`linux-glibc-x86_64`、container root） | §6 / [validation-results.md](platforms/validation-results.md) |
| E02 | HAOS x86_64 Alpine/musl add-on | canonical runtime（`linux-musl-x86_64`、Supervisor add-on） | §6 / validation-results.md |
| E03 | AnduinOS x86_64 | canonical Linux x86_64 runtime | §6 / validation-results.md |
| E04 | macOS arm64 | canonical runtime（`darwin-arm64`） | §6 / validation-results.md |
| E05 | Android Termux aarch64 | canonical runtime（`android-aarch64`、2-FD） | §6 / validation-results.md |
| E06 | Android Termux armv7a | canonical runtime（`android-armv7a`、2-FD） | §6 / validation-results.md |
| E07 | Bliss OS x86_64 Termux | canonical runtime（`android-x86_64`、2-FD） | §6 / validation-results.md |
| E08 | Fedora x86_64（SELinux） | historical-only / conditional（`packaging/fedora/**` 変更時） | §6 / [Fedora手順](../packaging/fedora/README.md) |
| E09 | Gentoo x86_64 | historical-only / conditional | §6 / validation-results.md |
| E10 | NixOS x86_64 | historical-only / conditional | §6 / [NixOS](platforms/nixos.md) |
| E11 | Chimera Linux x86_64 | historical-only / conditional | §6 / [Chimera](platforms/chimera-linux.md) |
| E12 | Alpine x86_64（mdev） | historical-only / conditional（`packaging/mdev/**` 変更時） | §6 / [mdev](platforms/alpine-mdev.md) |
| E13 | OpenWrt x86_64 | historical-only / conditional | §6 / validation-results.md |
| E14 | FreeBSD x86_64 | 対象外（SPEC §1・§2の対象外。記録は履歴のみ） | 対象外 |
| E15 | Fedora aarch64 | `linux-glibc-aarch64`必須matrix環境。既存記録はhistorical baseline | §6 / validation-results.md |
| E16 | Debian x86/i386 | source-build-only（i386配布artifactなし） | §6 / [Debian i386](platforms/debian-i686.md) |
| E17 | Windows 11 x86_64 | Phase 1 build/offline test。実機matrixは未完了（`hardware-unverified`） | §6 / validation-results.md |
| — | Android ad-hoc APK | 対象外（dtv-android 所管。本リポジトリの gate に含めない） | 対象外 |

### 毎回必須の短時間実機matrix

候補artifact自体をnative architectureで起動する。各行は別々に実施・記録し、同じarchitectureの別artifactや別OSの結果で置き換えない。Linux musl aarch64はE15のnative Fedora aarch64上でAlpine arm64 Docker containerを使い、CPU emulationなしで実施する。

| 配布binary archive | 必須環境 | 毎回行う実機確認 |
|---|---|---|
| `linux-glibc-x86_64` | E03 AnduinOS x86_64 | 列挙、B-CAS抜去・再挿入（不在検出、ATR/reset/APDU復帰）、8 receiverの短い受信、USB切断・再接続と復旧確認 |
| `linux-musl-x86_64` | E02 HAOS Alpine/musl add-on | 同上。試験用add-onの起動停止とSupervisor設定復元を含む |
| `linux-glibc-aarch64` | E15 Fedora aarch64 / L4T | 同上。native artifactを使う |
| `linux-musl-aarch64` | E15 Fedora aarch64上のAlpine arm64 Docker container | 同上。native aarch64 host上で実行し、CPU emulationを使わない。物理USB deviceをcontainerへ渡す |
| `darwin-arm64` | E04 macOS arm64 | 同上。native `darwin-arm64` artifactを使う |
| `android-aarch64` | E05 Termux aarch64 | 同上。2-FD launcher経由で受信する |
| `android-armv7a` | E06 Termux armv7a | E05と同じ手順、armv7a archive |
| `android-x86_64` | E07 Bliss OS Termux x86_64 | E05と同じ手順、x86_64 archive |
| `windows-x86_64` | E17 Windows 11 x64 | E17のWindows手順。列挙・ready、短い受信、CARD ATR/reset/反復APDU、カード抜去・再挿入、USB切断・再接続後のdaemon再起動、cooperative shutdown後の残留確認 |

このmatrixはrelease archive manifestの9 binary target（8 tar archiveとWindows ZIP 1つ）と、Q3U4の既存実機記録に基づく。過去の受信・抜差し結果は[validation-results.md](platforms/validation-results.md)に記録されている。[PR #15](https://github.com/Khronos31/px4-userland/pull/15)はfinal candidate artifact auditとhardware canaryを記録し、[Issue #4](https://github.com/Khronos31/px4-userland/issues/4)はusbip・LSM・libusb/access pathなどディストリビューション名だけでは覆えない検証軸を整理している。過去記録は今回candidateのmatrix結果を代替しない。

各artifactの実機確認は列挙、短い受信、カード状態確認、B-CAS抜去/再挿入、USB切断/再接続、復旧確認を順に実行する。これら一連の確認に総時間上限を設けない。5分の上限はユーザーへ物理操作を依頼してから操作が完了するまでの待機だけに適用する。候補archiveのchecksum・展開、APDU、RF/電源/firmware、host準備は検証開始前に済ませる。5分以内に物理操作が行われなければ、その操作を未完了として記録し、物理deviceを使う工程を中断する。5分を超える連続負荷試験は短時間確認に混ぜず、ユーザーが決定するsoakへ分ける。

用語:

- **canonical runtime**: その artifact の実機検証を代表する環境。同一 executable を共有する runtime（例: E01/E02/E03 の Linux x86_64）でも、access path が違えば証拠は自動継承しない。
- **conditional**: その環境に固有の材料（配布設定、構成手順）を変更したときだけ実行する。
- **source-build-only**: 配布 artifact を持たず、source archive の native build だけで記録する。
- **historical-only**: 過去の記録だけがあり、current claim ではない。
- **unverified**: build/CI は通るが実機 claim がない。

### 0.1 共通コマンド

`<version>`、`<platform>`、`$ID`、`$FW`、`$RT`、`$LOG` は実値に置き換える。artifact は候補 commit の `release-candidate`（§2）だけを使い、手元 build や旧 archive で代用しない。

artifact 検証と展開（X-PREP）:

```sh
sha256sum -c SHA256SUMS                              # macOS: shasum -a 256 -c
D=$(mktemp -d); tar -xzf px4-userland-<version>-<platform>.tar.gz -C "$D"
sha256sum "$FW"                                      # 5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484
RT=$(mktemp -d "${TMPDIR:-/tmp}/px4-userland.XXXXXX"); LOG=<log dir>
"$D/px4d" --list > "$LOG/list.txt"                   # model / usb / status=ready / receivers
```

daemon 起動と準備完了確認（X-START）:

```sh
"$D/px4d" --device "$ID" --firmware "$FW" --runtime-dir "$RT" 2> "$LOG/px4d.err" &
P=$!
# statusを再実行してreadyを確認する。group modeならpx4d/px4ctlの両方に --group を付ける。
"$D/px4ctl" --device "$ID" --runtime-dir "$RT" status | tee "$LOG/status-start.txt"
"$D/px4ctl" --device "$ID" --runtime-dir "$RT" list | tee "$LOG/ctl-list.txt"
```

受信と監視（X-LOAD / X-MON）:

次の8コマンドを同じ端末で個別に実行する。各receiverは30秒で終了する。これは受信確認であり、物理操作の応答を待つ間の負荷継続には使わない。

```sh
# Q3U4 8 receiver（受信継続: 0/1/4/5=S、2/3/6/7=T）
"$D/px4-ts" --device "$ID" --receiver 0 --system isdb-s --frequency-khz 1318000 --slot 0 --runtime-dir "$RT" --output /dev/null --duration-seconds 30 2> "$LOG/r0.err" & R0=$!
"$D/px4-ts" --device "$ID" --receiver 1 --system isdb-s --frequency-khz 1318000 --slot 0 --runtime-dir "$RT" --output /dev/null --duration-seconds 30 2> "$LOG/r1.err" & R1=$!
"$D/px4-ts" --device "$ID" --receiver 4 --system isdb-s --frequency-khz 1318000 --slot 0 --runtime-dir "$RT" --output /dev/null --duration-seconds 30 2> "$LOG/r4.err" & R4=$!
"$D/px4-ts" --device "$ID" --receiver 5 --system isdb-s --frequency-khz 1318000 --slot 0 --runtime-dir "$RT" --output /dev/null --duration-seconds 30 2> "$LOG/r5.err" & R5=$!
"$D/px4-ts" --device "$ID" --receiver 2 --system isdb-t --frequency-khz 527143 --runtime-dir "$RT" --output /dev/null --duration-seconds 30 2> "$LOG/r2.err" & R2=$!
"$D/px4-ts" --device "$ID" --receiver 3 --system isdb-t --frequency-khz 527143 --runtime-dir "$RT" --output /dev/null --duration-seconds 30 2> "$LOG/r3.err" & R3=$!
"$D/px4-ts" --device "$ID" --receiver 6 --system isdb-t --frequency-khz 527143 --runtime-dir "$RT" --output /dev/null --duration-seconds 30 2> "$LOG/r6.err" & R6=$!
"$D/px4-ts" --device "$ID" --receiver 7 --system isdb-t --frequency-khz 527143 --runtime-dir "$RT" --output /dev/null --duration-seconds 30 2> "$LOG/r7.err" & R7=$!
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

受信中の監視は別の端末で実行する: `px4ctl status`、`px4ctl card-apdu 90:30:00:00:00 --repeat 10`、FD/RSSを記録する。受信確認の実行時間は30秒で、全手順やartifact gateの所要時間を制限しない。

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

Q3U4 preflight: 両 USB device、15V adapter、両 RF lead を接続し、`px4d --list` が `status=ready receivers=8` を返すことを確認してから開始する。

カードの物理抜去・再挿入（X-CARD-HP、毎回必須）:

以下は§0.1のdaemonを起動したまま、receiver captureを行っていない状態で実施する。HAOS/SCS側で各物理操作を依頼する直前に通知コマンドを実行する。Codexは`beep`、Claude Codeは`vibe`を使い、通知後にユーザーへ操作を依頼してから最大5分待つ。5分はその物理操作1回の応答待ちだけに適用する。要求した操作が未完了ならその操作とartifact gateを未完了として記録し、次へ進まない。

カード挿入状態を確認するコマンド群（個別に実行）:

```sh
CTL="$D/px4ctl"
APDU=90:30:00:00:00
"$CTL" --device "$ID" --runtime-dir "$RT" card-status | tee "$LOG/card-before.txt"
"$CTL" --device "$ID" --runtime-dir "$RT" card-atr > "$LOG/card-before.atr"
"$CTL" --device "$ID" --runtime-dir "$RT" card-reset > "$LOG/card-before.reset"
"$CTL" --device "$ID" --runtime-dir "$RT" card-apdu "$APDU" --repeat 10 > "$LOG/card-before.apdu"
```

カード抜去を要求する前にHAOS/SCS側で通知する:

```sh
beep   # Codex。Claude Codeは vibe
```

ユーザーへB-CASカードを抜くよう依頼し、操作完了まで最大5分待つ。その後に不在検出コマンドを個別に実行する:

```sh
"$CTL" --device "$ID" --runtime-dir "$RT" card-status | tee "$LOG/card-removed.txt"
if "$CTL" --device "$ID" --runtime-dir "$RT" card-atr > "$LOG/card-removed.atr" 2> "$LOG/card-removed.err"; then rc=0; else rc=$?; fi
echo "$rc" > "$LOG/card-removed.atr.rc"       # NO_CARD / exit 9 を期待
if "$CTL" --device "$ID" --runtime-dir "$RT" card-apdu "$APDU" > "$LOG/card-removed.apdu" 2> "$LOG/card-removed-apdu.err"; then rc=0; else rc=$?; fi
echo "$rc" > "$LOG/card-removed.apdu.rc"      # NO_CARD / exit 9 を期待
```

カード再挿入を要求する前にHAOS/SCS側で再度通知する:

```sh
beep   # Codex。Claude Codeは vibe
```

ユーザーへB-CASカードを戻すよう依頼し、操作完了まで最大5分待つ。その後に復帰コマンドを個別に実行する:

```sh
"$CTL" --device "$ID" --runtime-dir "$RT" card-status | tee "$LOG/card-reinserted.txt"
"$CTL" --device "$ID" --runtime-dir "$RT" card-atr > "$LOG/card-reinserted.atr"
"$CTL" --device "$ID" --runtime-dir "$RT" card-reset > "$LOG/card-reinserted.reset"
"$CTL" --device "$ID" --runtime-dir "$RT" card-apdu "$APDU" --repeat 10 > "$LOG/card-reinserted.apdu"
```

判定: `card-status`の`present=no`とreader generationの変化、ATR/APDUの`NO_CARD`・exit 9を確認する。再挿入時は`present=yes`となりgenerationが再度変化し、ATR取得・reset・APDU 10回が成功することを確認する（過去の正常応答はSW `90 00`）。その後§0.1の8 receiverを30秒実行し、TS/counter条件はSPEC 10.2に従う。カード抜去・再挿入ができない場合、そのartifactの短時間gateは未完了。

USBの物理切断・再接続（X-USB-HP、毎回必須）:

カードを挿入した状態でdaemonを起動し、8 receiver captureは止めておく。USB切断を観測するためのclientを1つだけ起動する。HAOS/SCS側で物理操作を依頼する直前に通知し、操作完了まで最大5分待つ。要求した操作が未完了ならその操作とartifact gateを未完了として記録し、次へ進まない。Q3U4は筐体の物理USBケーブル1本を外すと、2つの内部USB deviceが切断される。

USB切断を観測するclientを起動する:

```sh
"$D/px4-ts" --device "$ID" --receiver 0 --system isdb-s --frequency-khz 1318000 --slot 0 \
  --runtime-dir "$RT" --output /dev/null --duration-seconds 300 2> "$LOG/usb-detach-client.err" &
USB_CLIENT=$!
```

USB切断を要求する前にHAOS/SCS側で通知する:

```sh
beep   # Codex。Claude Codeは vibe
```

ユーザーへ対象tunerのUSB接続を外すよう依頼し、操作完了まで最大5分待つ。5分以内に操作がなければclientがduration満了で終了していても、物理操作とartifact gateを未完了として記録する。まだclientが動いている場合は停止する。USB切断によりclientが終了した場合は、終了値を記録して検査する（`DISCONNECTED` / exit 7を期待）:

```sh
wait "$USB_CLIENT"; echo "$?" > "$LOG/usb-detach-client.rc"
"$D/px4ctl" --device "$ID" --runtime-dir "$RT" status > "$LOG/usb-detach-status.txt" 2>&1
```

切断したUSB接続を戻すようユーザーに依頼する直前に、HAOS/SCS側で再度通知する:

```sh
beep   # Codex。Claude Codeは vibe
```

ユーザーへUSB接続を戻すよう依頼し、操作完了まで最大5分待つ。再列挙・復旧コマンドを個別に実行する:

```sh
"$D/px4d" --list | tee "$LOG/list-reconnected.txt"
"$D/px4d" --list-json > "$LOG/list-reconnected.json"
```

切断側clientが有限時間内に`DISCONNECTED` / exit 7で終了し、ハングやclient process残留がないことを確認する。過去の実機確認では切断後も旧daemonが生存し、USBを戻して再列挙されても自動復帰しなかった。再接続後の`--list` / `--list-json`で対象device（Q3U4は2つの内部USB device）が再列挙されたことを記録するが、これはdaemon復旧の合格とはしない。

旧daemonが生存している場合は、同じ候補のdaemonを停止して終了を待つ:

```sh
kill -TERM "$P"
wait "$P"
```

旧daemonがすでに終了している場合は`pgrep -fl '[p]x4d'`で残留がないことを確認する。どちらの場合も旧daemonを再利用せず、X-STARTのコマンド群を同じcandidate artifact・device ID・runtime dirで再実行し、`px4ctl status`でreadyを確認する。その後§0.1の8 receiver 30秒確認と、次のAPDU確認を行う:

```sh
"$D/px4ctl" --device "$ID" --runtime-dir "$RT" card-apdu 90:30:00:00:00 --repeat 10 \
  > "$LOG/card-after-usb-reconnect.apdu"
```

終了後にX-STOP / X-RESIDでdaemon/client process、FD、IPC socket/control endpointの残留がないことを確認する。USB切断・再接続ができない場合、そのartifactの短時間gateは未完了。

この再起動ベースの復旧は[既存の実機記録](platforms/validation-results.md)に基づく。Q3U4のHAOSでのケーブル再接続後の再列挙（[記録](platforms/validation-results.md#L37)）、Alpine muslの物理切断後の手動復帰（[HAOS add-on](platforms/validation-results.md#L263)）、OpenWrtでの切断exit 7・残留なし・再接続後の復帰（[記録](platforms/validation-results.md#L270)）、PX-M1UR/PX-S1URでの旧daemon停止後の復旧（[M1UR](platforms/validation-results.md#L141)、[S1UR](platforms/validation-results.md#L142)）がある。

### 0.2 same-lease retune（条件付き）

同一lease retuneは、IPC/lease/retune、frontend/tune、device identityの変更時に追加確認する。それ以外はCI offline試験の成功を記録する。hardwareで行う場合は、既存のout-of-repo `/config/.work/px4-m1ur-s1ur/retune-tool/retune_tool.cpp`を使ってよい。その場合はsourceとbinaryのSHA-256、およびbuild条件（compiler、flags、libusb）をrelease recordに記録する。新しいtoolをこのrepositoryへ追加しない。

### 0.3 receiver 7 参照比較（X-R7REF, 条件付き）

receiver 7 で TEI/continuity burst が出て、SPEC 10.2.6a の比較条件（同一個体・antenna・power・firmware・frequency・duration・同等 capture）を満たす fresh 参照が必要な場合にだけ行う。kernel module を load できる環境（E03 が canonical、E08–E12、E16 も可）で `tsukumijima/px4_drv` を一時 load し、同じ条件で counter を取得した後 unload する。module の load/unload はユーザー確認のうえ実施する。比較条件を再現できない場合は判定保留とし、pass 扱いしない。


## 1. 候補と前回証拠を固定する

1. 候補の `VERSION`、GitHub 上の source commit、candidate workflow run を特定する。dirty な作業ツリーや未 push のローカル変更を候補として試験しない。
2. 前回 Stable 以降の累積差分と、今回使う baseline evidence を確認する。差分をファイル名だけで判断せず、SPEC 10.5.1 の変更分類と実際の呼出経路・依存先から model/profile・runtime/access path・feature の影響範囲を決める。
3. 対象 claim ごとに `継承`、`今回再検証`、`未認定`、`対象外` のいずれかを記録し、根拠を書く。証拠の軸は SPEC 10.2.8 に従う。別 model、別 OS/runtime、別 access path、別 feature へ結果を外挿しない。同一 model・同一 runtime/access path・同一 feature の証拠は、対象 artifact の bytes が baseline と同一であるか、変更がその path へ影響しないと 10.5.1 の表で判定できる場合に継承できる。未知の影響は affected として扱い、`対象外` は SPEC の対象外または本手順 §0 の対象外に限る。
4. 前回の適格なlong soakの日付やrelease数は事実情報として記録できるが、エージェントがsoak有無・時間・OSを決める規則として使わない。

Termuxのarchitecture、Termux launcherのFD path、glibc/musl、native PC/SC adapterは別々のruntime/access pathとして扱う。Windows Phase 1はbuild/offline testと`build-tested` evidenceまでを対象とし、実機matrixが未完了の間は`hardware-unverified`である。FreeBSDとAndroid ad-hoc APKは対象外である。変更も新規claimもないpathを毎回試験しない。

影響分類が複数にまたがる、依存範囲が不明、または非影響を証明できない場合は `unknown/ambiguous` として、影響し得るpath集合をユーザーへの説明に含める。配布artifactごとの必須短時間matrixは影響判定に関係なく毎回実施する。

## 2. source / CI / candidate artifact gate（毎回）

1. 候補 commit に対応する `portable userland foundation` workflow を確認する。path filter 等により自動実行されていなければ、GitHub Actions の `workflow_dispatch` でその候補 ref を指定して実行する。
2. workflow 内の unit/offline test、各 target build、source/relink、`release-candidate` と、それに依存する4つの Ubuntu/Alpine × x86_64/aarch64 artifact smoke job、Windows cross-build/PE audit/packaging job、および同じWindows ZIPを実行するWindows native offline test job がすべて成功していることを確認する。失敗 job を無視して先へ進まない。`release-candidate` はWindows cross-buildとnative testの両方に依存し、native testが消費したものと同一のWindows ZIPを取り込む。test executable artifactは取り込まない。
3. `release-candidate` artifact が候補 commit の `source_ref` を示し、9 binary archive（8 tar archiveとWindows ZIP 1つ）と対応 source archiveの計10 archive、およびその外側 `SHA256SUMS`（10件）を含むことを確認する。チェックサムを照合し、CIの manifest・license・corresponding-source・binary/source archive audit（Windows ZIPのPE/import/archive監査を含む）が通っていることを確認する。
4. 工程2の試験完了前に候補source commitが変わった場合は、そのcommitのCIとartifactを取り直す。前のcommitのarchive、手元で別途作ったbuild、PR runの古いartifactを新candidateの代用にしない。工程3の試験結果commitは候補source commitの変更ではなく、工程4ではその結果commitにタグを付けてrelease CIを実行する。
5. release CIが生成した9 binary archiveを展開し、実機試験に使ったfinal candidate archiveと各payloadを比較する。更新された`README.md`、それを反映した`manifest.json`の`source_ref`およびREADMEのhash、内側の`SHA256SUMS`以外に差があれば公開を止めて原因を調べる。source archiveは工程3の記録commitを含むためbinary candidateのsource archiveとは異なる。比較したrun、archive、差異の判定をrelease recordへ記録する。

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

## 3. 配布artifactごとの実機短時間確認（毎回必須）

§0の9行すべてについて、candidate artifactそのものを使い、対応する環境で独立に実施する。Windows行はE17のWindows手順に従い、native Windows 11 x64で実施する。必須確認の一連の操作に総時間上限は設けない。B-CAS/USBの物理操作はユーザーが行い、各操作を依頼した時点から最大5分待つ。各物理操作を依頼する直前にHAOS/SCS側でCodexは`beep`、Claude Codeは`vibe`を実行する。各runの記録にはarchive名/SHA-256、source commit、環境ID/OS version/architecture、機種/USB path、実施時刻、コマンド、終了値、card status/generation/ATR/APDU応答、TS/counter、残留の有無を残す。

1. 候補archiveのhash確認・展開、Q3U4のfirmware/RF/電源preflight、ログ先作成を済ませる。これらを含めて一連の確認に総時間上限は設けない。
2. `px4d --list` と `--list-json` でQ3U4が8 receiver readyと列挙されることを確認し、JSON schema・boolean型・receiver capability値を照合する。M1UR/S1URのserial衝突やLNB非対応profile表示が変更対象なら、§0の該当する機種別追加確認も行う。
3. daemonを起動したままreceiver captureを止め、X-CARD-HPのコマンド群を実行する。カード抜去を依頼する直前に通知し、依頼後最大5分待つ。`card-status`の`present=no`、ATR/APDUの`NO_CARD`（exit 9）を確認する。
4. 再挿入を依頼する直前に通知し、依頼後最大5分待つ。generation更新、ATR/reset/APDU 10回の復帰を確認する。
5. X-USB-HPで1 receiver clientを起動してUSB切断を観測する。USB切断を依頼する直前に通知し、依頼後最大5分待つ。clientが有限時間で`DISCONNECTED` / exit 7となり、ハングやclient process残留がないことを確認する。
6. USB再接続を依頼する直前に通知し、依頼後最大5分待つ。`--list` / `--list-json`で再列挙を確認した後、旧daemonを停止して終了を待ち、X-STARTの手順で同じcandidateからdaemonを起動し直す。古いdaemonの自動復帰を合格条件にしない。
7. 再起動後に`px4ctl status`でreadyを確認し、8 receiverを30秒実行してTS条件をSPEC 10.2の適用項目で確認する。APDU 10回も再確認し、最後にdaemon/client、FD、IPC socket/control endpointの残留がないことを確認する。

Linux musl aarch64はE15 Fedora aarch64上のAlpine arm64 Docker containerでcandidate artifactを実行する。hostがaarch64であることを`uname -m`等で確認し、CPU emulationを使わず、物理USB deviceをcontainerへ渡す。このartifact行はglibc aarch64の結果で代用せず、独立して実施・記録する。native aarch64 hostまたはUSB passthroughが利用できず物理確認を完了できない場合はgate未完了とする。

同一lease retuneは§0.2に従い、該当するコード変更時の追加確認とする。receiver 7のTEI/continuity burstは自動合格にせず、SPEC 10.2.6aの比較条件に従う。

## 4. 変更影響の追加確認とsoak（ユーザー決定）

毎回必須の短時間matrixとは別に、SPEC 10.5.1の分類、hunk/call path、platform guard、過去のvalidation record、candidateで観測したcounterを使い、影響し得るmodel/profile/runtime/access pathを整理する。この整理はsoak有無やtarget OSをエージェントが決める権限を与えない。

安定性に影響し得るコード変更があれば、エージェントは次をユーザーへ提示し、回答を得るまでsoakを開始しない。

- 変更と安定性への具体的な影響経路、および影響し得るOS/artifact/機種
- 既存証拠で今回分かること、今回の短時間matrixで得た結果、未確認点
- ユーザー選択肢: soakなし / 10分 / 30分 / 2時間。実施する場合は対象OSもユーザーが指定

ユーザーはsoakを行わない選択もできる。選択された時間とOSを記録し、その指定範囲だけ試験する。agentはdiff class、canonical順、影響範囲、機材都合から時間・有無・OSを既定または自動決定しない。候補選択を求められた場合に限り、判断材料を比較して提示し、決定を待つ。

ユーザーがsoakを選んだときの実行方法は次の通り。

- 10分/30分: 指定OSで指定profileの連続受信を行い、該当するstatus/card/retune/stop-reopenなどの挙動を記録する。
- 2時間: 指定OSで、選択されたprofileに応じた負荷、反復status/APDU、retune/stop-reopen、FD数・RSSの経時傾向、cleanupを確認する。Q3U4では§0.1の8 receiver T/S混在を使う。
- いずれもshell終了値だけで合否を決めない。TS/counterはSPECの受入条件を使う。FD/RSSは数値固定の合否閾値を設定せず、非安定な持続増加は`判定保留`として調査する。

追加profile認定、single receiver認定、PX-M1UR/PX-S1UR同一serial確認、receiver 7比較の個別条件はSPEC 10.2.6a/10.2.7と各記録を参照する。これらの認定を毎回必須の9-artifact短時間matrixの代わりに使わない。

### PX-M1UR / PX-S1UR の同一serial確認

v0.26 の識別変更が対象のとき、両機種を同時接続できる環境では、§2 の exact candidate を使って `px4d --list` と `px4d --list-json` を個別に実行し、同じ serial の2筐体が別機種・別USB位置として現れ、双方の `serial_unique` が `false` であることを記録する。通常列挙を許さない Termux では、このコマンドの成功を要求しない。

実施可能なら、同じ状態で `px4d --device 000000000000001 --firmware "$FW" --runtime-dir "$RT"` の曖昧指定が候補と `--usb-path` を示して exit 2 となり、USB interface の claim と endpoint 公開の前に失敗することを確認する。stdout/stderr・終了コードとendpoint残存の有無を保存し、claim前拒否の根拠には模擬USB試験も対応付ける。この同時接続中は、受信・カード・LNB給電・電源制御の試験を行わない。物理USBの抜き差しは利用者の確認を得て行う。実施できない環境では未実施と理由を記録し、順次接続のcanaryや認定結果を同時接続時の識別結果へ流用しない。

### LNB capability表示・15V非対応profileの拒否確認

`lnb_15v_supported`または15V非対応profileの変更時は、exact candidateでM1URとS1URを同時接続し、上の同一serial確認と同じnative列挙環境で`--list`と`--list-json`を実行する。テキストとJSONの両方で、M1URのISDB-T/S receiverとS1URのISDB-T receiverが`lnb_15v_supported=false`であることを確認する。Q3U4を同時に列挙した場合は、ISDB-T receiverがfalse、ISDB-S receiverがtrueであることも確認し、falseだけを一律に出す誤りを検出する。JSONでは値が文字列ではなくbooleanであることを確認する。Android Termuxは通常のUSB列挙を保証しないため、このlist確認を要求しない。

PX-M1URのISDB-SはLNB 0Vで受信できるが、15V要求は`--allow-lnb-power`の有無によらず`UNSUPPORTED`（exit 3）で拒否され、LNB用GPIOへ書き込まない。LNB/GPIO動作を変更したcandidateでは、PX-M1URだけを接続し、同じS受信要求をdaemonのopt-inなし・ありの両方で試す。daemon起動には毎回X-STARTのコマンド群を使い、なしのrunは通常のX-START、ありのrunは`--allow-lnb-power`を追加して起動し、各run後にdaemonを停止する。各daemon起動後に、次のコマンドを実行してexit 3を記録する:

```sh
if "$D/px4-ts" --device "$ID" --receiver 0 --system isdb-s --frequency-khz 1318000 --slot 0 \
  --runtime-dir "$RT" --lnb-voltage 15 --duration-seconds 1 --output /dev/null \
  > "$LOG/lnb-15v-rejected.out" 2> "$LOG/lnb-15v-rejected.err"; then rc=0; else rc=$?; fi
echo "$rc" > "$LOG/lnb-15v-rejected.rc"
```

opt-inなし・ありの各runでログ名を分ける。15V拒否時に電圧が出ないことの確認はoffline/mock GPIO試験に結び付け、実機で15Vを許可する試験へ置き換えない。S1URはISDB-T専用なので、その15V確認には使わない。変更が列挙表示だけでLNB/GPIO要求経路に触れていない場合、この物理15V拒否試験は追加せず、表示確認だけを行う。

### USB/cardの物理抜差し

毎回必須の短時間確認では、matrix各行でB-CASカードの抜去/再挿入とUSB切断/再接続をユーザーが実施する。daemonを起動したままcard hotplugを行い、不在検出と再挿入後のATR/reset/APDU復帰を確認してから、30秒の8 receiver captureでTS/counterを確認する。各物理操作の応答待ち上限は個別に5分とし、要求直前にHAOS/SCS側でCodexは`beep`、Claude Codeは`vibe`を実行する。新しい`tuner-hardware-verified` / `card-core-hardware-verified` claimでは、SPEC 10.3が要求する各抜差しをそのclaimのqualificationで追加実施する。必須抜差しができなければ、そのartifact/claimは未完了または未認定と記録する。

## 5. 結果の記録と公開可否

v0.1.10のreceiver 7 burstとmacOSの単発CC異常には、SPEC v0.29 §10.5に記録したユーザー決定の
release限定dispositionを適用する。原試行と追加試験を別々に保存し、参照比較の全試行合格や
原因解明、一般的な異常免除を意味する表示に置き換えない。

ハードウェア試験は [`platforms/validation-results.md`](platforms/validation-results.md) に日付付きで追記する。新しい records directory や template framework、汎用スクリプトは作らない。Stable release recordには少なくとも次を残す。

- version、source commit、candidate workflow run、10 archive（9 binary archiveとsource archive）と外側checksumの確認結果、toolchain/build input、static/dynamic link inventory、relink結果、license/corresponding-source条件
- 前回Stable tag、使用したbaseline evidence、baseline以後の累積差分、各artifactのbinary byte-identity判定
- 変更impact分類と hunk-level の call-path / guard 適用条件、対象/除外したmodel-profile・runtime/access path・featureと根拠
- claimごとの `継承` / `今回再検証` / `未認定` / `対象外`、exact candidateでの試験有無
- 必須matrixの各binary archiveと対応環境、各実機testの環境ID・host・device/USB ID・runtime/access path・archive SHA-256・日時（UTC）・コマンド・counter・結果・ログ保存先
- soakについてユーザー決定（実施有無・時間・OS）と実施結果。receiver 7 fresh comparison、USB/card抜差しが追加で必要な場合は実施状態と理由
- soakのFD数・RSS記録系列と、`安定` / `頭打ち` / `判定保留` のdispositionおよび理由
- 残存blocker、既知の非blocking制限、README/support表示とrelease noteへの反映

失敗した試行は消さずに記録し、原因を切り分けた後の再試験を別試行として残す。受信設備不備や誤ったコマンド等を特定できても、失敗をpassに書き換えない。

次をすべて満たすまでStable公開へ進まない。

- Stable共通のCI、candidate artifact audit、source/license、checksum gateが成功。
- final candidateの全配布binary artifactについて短時間matrixが成功し、ユーザーがsoakを選んだ場合は指定時間・OSの試験が完了している。soakのFD数・RSSが最終観測まで安定化しない持続的な増加を示し、外部要因を特定できない場合は `判定保留` とし、公開しない。
- READMEの各support claimが実証または適格なbaseline継承に対応し、未試験のmodel × runtime/access path × featureを認定表示していない。
- crash、hang、use-after-free、stale lease、再接続不能、カード経路の重大な未解決issueがない。
- 短時間matrixに未完了の配布artifactがある場合はStable gate未完了とし、そのままsupport表示だけで完了扱いしない。
- SPEC 10.5-2の再現性確認（同一source commitの独立2 runによる10 archive（Windows ZIPを含む）と外側`SHA256SUMS`のbyte一致）を、未実施のままpass扱いしない。toolchain/build inputが異なる比較はinconclusiveとして成功とせず、matching pairを取り直す。正規化比較による合格は認めない。
- README、LICENSE、THIRD_PARTY_NOTICES、provenance、checksum、support表示、release archiveの内容が一致し、公開前レビュー済み。
- 現行 inventory に無い機種は実機未検証としてREADMEに明示すればリリース可能。Betaを機種追加の代わりに使わない。

リリースノートには利用者向け変更、短い検証結果、必要な既知制限だけを書く。試験matrix、内部証拠系譜、詳細log、hash一覧は掲載せず、本記録とREADMEへ分離する。

## 6. 環境別手順（E01–E17）

各項は §0.1 の共通部品と次の追加手順で構成する。物理操作（USB/card の抜差し、アンテナ、電源、kernel module blacklist変更）はユーザーが事前確認のうえ実施する。HAOS（E01/E02）は production host であり、Supervisor 状態変更は承認・退避・復元を伴う。サービスを再起動しない。

### E01 HAOS x86_64 Debian/glibc SCS（canonical, container root）

- 前提: `ha core info` で HAOS version と kernel を記録する。container root 実行であることを記録し、非root claim に使わない。`lsusb -t` で kernel px4 driver が掴んでいないことを確認する。`linux-glibc-x86_64` と glibc IFD を使う。
- 手順: §0.1 の X-PREP / X-START / X-LOAD / X-MON / X-STOP / X-RESID。Q3U4 は §0.1 の preflight を先に行う。PC/SC は container の `pcscd` を使い、終了後に元の状態へ戻す。
- 選定: 影響artifactが `linux-glibc-x86_64` のとき、または HAOS container 層が変わったとき。

### E02 HAOS x86_64 Alpine/musl add-on（canonical, Supervisor）

- 前提: Supervisor add-on の options を GET 相当の現行値で退避し、ユーザー承認のうえ試験用 options を適用して add-on を起動する。終了後に options を復元して add-on を停止する。`ha`/Supervisor の状態変更はこの範囲に限り、HA Core・mirakc を再起動しない。`linux-musl-x86_64` と musl IFD を使う。
- 手順: E01 と同じ。Q3U4 は §0.1 の preflight を先に行う。物理ホットプラグを行う場合はユーザーが実施する。
- 選定: `linux-musl-x86_64`配布artifactの必須実機確認。

### E03 AnduinOS x86_64（canonical Linux x86_64）

- 前提: `smsusb`/`smsdvb`/`smsmdtv` を blacklist し、`lsusb -t` で未bindを確認する（siano側の kernel module を掴む場合）。px4 は kernel driver 不要だが、他製品の module 干渉がないことを確認する。udev rule と `video` グループを用意し、非root で実行する。
- 手順: §0.1。Q3U4は§0.1のpreflightを先に行う。`linux-glibc-x86_64`必須matrix環境。X-R7REFを行う場合は`tsukumijima/px4_drv`を一時loadし、同一個体・antenna・power・firmware・frequency・durationで取得後unloadする（module unloadもユーザー確認対象）。
- 選定: `linux-glibc-x86_64`配布artifactの必須実機確認。

### E04 macOS arm64（canonical darwin）

- 前提: `shasum -a 256 -c SHA256SUMS`。`otool -L` で libusb dylib と macOS system 以外の依存がないことを確認する。`sw_vers` を記録する。PC/SC は Homebrew `pcsc-lite` と `ifd/px4-userland-ifd.bundle` を使う。
- 手順: §0.1（`stat -f %z`、`lsof -p`、`ps -o rss=` を用いる）。`darwin-arm64` を使う。
- 選定: `darwin-arm64`配布artifactの必須実機確認。

### E05 Android Termux aarch64（canonical android-aarch64, 2-FD）

- 前提: Termux と Termux:API（`termux-usb`）、`util-linux`（`setsid`）を用意する。`android-aarch64` の `px4-termux` を使う。native PC/SC adapter は N/A（card は portable IPC 経由）。
- 手順: §0.1 の Termux 2-FD。launcher の SIGINT/SIGTERM/SIGHUP と 40秒猶予、第2 open 失敗、終了後 process/FD/endpoint 残存なしを確認する。
- 選定: `android-aarch64`配布artifactの必須実機確認。

### E06 Android Termux armv7a（canonical android-armv7a, 2-FD）

- 前提・手順・選定: E05 と同じ。`android-armv7a` を使う。APK は対象外であり、同 device の APK 試験は dtv-android 側で行う。

### E07 Bliss OS x86_64 Termux（canonical android-x86_64, 2-FD；毎回matrix）

- 前提: 給電中の画面常時点灯と Termux 前面表示を検証中だけ使い、`stay_on_while_plugged_in` を実行前の値へ戻す。`android-x86_64` を使う。
- 手順: E05 と同じ。初回起動の `TIMEOUT` が起きた場合は初回起動の信頼性を未確定として記録し、合格値に埋めない。launcher に渡した device path を記録する。
- 選定: `android-x86_64`配布artifactの必須実機確認。

### E08 Fedora x86_64（SELinux, historical-only / conditional）

- 前提: `packaging/fedora/**` または Fedora 構成手順を変更したときだけ実施する。record `29635988c5692eb9167dc082ef0b4c4e7dfb5e04` は履歴であり current claim ではない。
- 手順: [Fedora 手順](../packaging/fedora/README.md) の手順、offline の `scripts/test-fedora.sh`、SELinux domain、一般ユーザー IPC、PC/SC、`ausearch -m AVC`。
- 選定: 当該材料の変更時のみ。配布artifactの必須matrix環境ではない。

### E09 Gentoo x86_64（historical-only / conditional）

- 記録は未commit差分に基づくため再現可能な current claim ではない。Gentoo 手順を新設・変更した場合に §0.1 を native build と OpenRC/udev `usb` グループ構成で実施する。それ以外は履歴のみ。

### E10 NixOS x86_64（historical-only / conditional）

- 前提: [NixOS 構成例](platforms/nixos.md)。record `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e` は履歴。
- 手順: §0.1 を native build で行い、宣言的 blacklist/udev を確認する。選定: 構成手順の変更時のみ。

### E11 Chimera Linux x86_64（historical-only / conditional）

- 前提: [Chimera 構成例](platforms/chimera-linux.md)。record `3e6e32a107566689a9b4bf223dca1e4e89f67cfa` は履歴。
- 手順: §0.1 を Clang/musl native build で行い、8 receiver と direct APDU を確認する。選定: 構成手順の変更時のみ。

### E12 Alpine x86_64 / mdev（historical-only / conditional）

- 前提: [Alpine/mdev 構成例](platforms/alpine-mdev.md)。`packaging/mdev/**` は配布物に含まれるため、変更時はこの環境を実施する。offline の `scripts/test-mdev.sh` も使う。record `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e` は履歴。
- 手順: `mdev -s`、非root 実行、8 receiver、direct APDU、cleanup。選定: `packaging/mdev/**` または構成手順の変更時のみ。

### E13 OpenWrt x86_64（historical-only / conditional）

- record は `29635988c5692eb9167dc082ef0b4c4e7dfb5e04` と同内容で、OpenWrt 向けソース修正はない。OpenWrt 手順を新設・変更した場合にだけ実施する。record なしのため current claim にしない。

### E14 FreeBSD x86_64（対象外）

SPEC §1 の対象環境（Linux、Android、macOS）に含まれないため 対象外。validation-results.md の FreeBSD record は履歴であり、release gate・canary・long soak に含めない。

### E15 Fedora aarch64（linux-glibc-aarch64必須matrix）

- 現状: `linux-glibc-aarch64`配布artifactの必須matrix環境。過去record `df6a1e634e5bec11961a1f0f15eedd1da22f7fee`はbaselineであり、candidateごとの短時間確認を代替しない。
- 手順: 毎回の短時間matrixではnative `linux-glibc-aarch64` candidateで列挙・受信・USB抜差し・再接続後の受信を行う。`uname -r`、`getenforce`を記録する。新たなhardware claimの認定はSPEC 10.3に従い別途行い、kernelを記録してclaimをそのkernelに限定する。
- `linux-musl-aarch64`の必須確認では、同じnative Fedora aarch64 host上のAlpine arm64 Docker containerでcandidate artifactを実行する。CPU emulationを使わず、対象tunerの物理USB deviceをcontainerへ渡す。glibc確認とは別のrunとして記録する。
- 選定: `linux-glibc-aarch64`配布artifactの必須実機確認。

### E16 Debian x86/i386（source-build-only）

- i386 配布 artifact は存在しない。source archive を native build し、CTest と Q3U4 の T/S 同時受信、内蔵 card APDU を確認する（[Debian i386](platforms/debian-i686.md)）。record は Stable `v0.1.3` source archive であり履歴。選定: 該当構成手順の変更時のみ。

### E17 Windows 11 x86_64（Windows archive必須matrix・Phase 1）

- 位置づけ: `windows-x86_64`配布artifactの必須matrix環境。CIのbuild/offline test成功は`build-tested`のみを
  示し、本節が完了するまで当該archiveのgateは未完了、support表示は`hardware-unverified`である。exact candidate
  archive（`px4-userland-<version>-windows-x86_64.zip`）をnative Windows 11 x64（実機）で実行する。
  `windows-2022` CIはtest OSであり、Windows 11実機の代用にしない。
- Phase 1範囲: `px4d`/`px4-ts`/`px4ctl`とversioned local IPCのCARD_*経路だけを使う。WinSCard互換DLLと
  Microsoft PC/SC IFD登録はPhase 2以降で本節の対象外であり、native-card-adapter列は`該当なし（N/A）`とする。
- preflight（read-only）:
  - `$PSVersionTable.PSVersion`、`[System.Environment]::OSVersion`、`chcp`のdefault code page、`$env:LOCALAPPDATA`を記録する。
  - WinUSB backend: `Get-PnpDevice -PresentOnly`と`pnputil /enum-drivers`で対象USB ID（`0511:xxxx`）の現driver
    bindingをinventoryとして記録する。ユーザー確認なしにWinUSB INF・driverのinstall/変更/削除
    （`pnputil /add-driver`・`/delete-driver`等）を行わず、kernel driverやINFを導入しない。
  - PX-S1UD/mirakc等の稼働経路を変更しない。対象Q3U4をclaim中の他daemonがあれば`px4d`が`busy`で失敗することを
    確認する。firmwareは利用者が用意した既存ファイルを`--firmware`で指定するだけで、download・抽出・変換をしない。
- runtime dir / ACL:
  - `$env:LOCALAPPDATA`配下の短いpathを`$RT`として`--runtime-dir`へ渡す。既定動作では`px4d`自身が
    same-user private ACLでruntime dirとendpointを作成するので、検証側で事前作成しない。
  - `$RT`を検証側で事前作成する必要がある場合に限り、elevated tokenでの`New-Item`はownerが
    `BUILTIN\Administrators`になり`px4d`のvalidatorに拒否される（native Windowsで観測）。その場合はownerを
    現在user SIDに明示設定し、継承を切ったprotected DACLで現在userだけに許可する。これを満たせない、または
    securityを弱める回避（broad ACL、public TEMP、owner検査の省略）は行わず、gate未完了として記録する:

    ```powershell
    $me = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = New-Object System.Security.AccessControl.DirectorySecurity
    $acl.SetOwner($me)
    $acl.SetAccessRuleProtection($true, $false)
    $acl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule(
      $me, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')))
    [System.IO.Directory]::CreateDirectory($RT, $acl) | Out-Null
    ```

  - endpointがsame-user private ACLであること、non-ASCII（UTF8）のruntime/firmware/output pathでもargv・
    file openがACPで壊れないことを確認する。
- コマンド（PowerShell。POSIX風の`/dev/null`・`sha256sum`・`$?`をそのまま使わない。空白入りpathに耐えるため
  引数は配列で組み立て、`Start-Process -ArgumentList`の文字列連結は使わない）。`$D`はタスクごとの新しい
  pathを使い、既存treeを`Expand-Archive -Force`で上書きしない。`$LOG`は使用前に作成する。redirectした
  stdout/stderrは必ず`ReadToEndAsync`で drain してから待つ（drainしないとpipeが埋まり`WaitForExit`がhangする）:

  ```powershell
  $ErrorActionPreference = 'Stop'
  $D = 'C:\px4-e17-<taskid>\extract'      # タスク固有の未使用path。既存pathを再利用しない。
  $RT = Join-Path $env:LOCALAPPDATA 'px4-e17-<taskid>'
  $LOG = 'C:\px4-e17-<taskid>\log'
  $ZIP = 'C:\path\to\px4-userland-<version>-windows-x86_64.zip'
  if (Test-Path $D) { throw "refusing to reuse existing extraction path: $D" }
  New-Item -ItemType Directory -Path $D | Out-Null
  New-Item -ItemType Directory -Path $LOG | Out-Null
  Get-FileHash -Algorithm SHA256 $ZIP
  Expand-Archive -Path $ZIP -DestinationPath $D   # fresh $D への展開。-Force で既存を上書きしない。

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
  function Start-Ts([int]$receiver, [string]$system, [int]$frequency, [string]$slot) {
    $a = @('--device', $ID, '--receiver', "$receiver", '--system', $system, '--frequency-khz', "$frequency")
    if ($slot) { $a += @('--slot', $slot) }
    $a += @('--runtime-dir', $RT, '--output', 'NUL', '--duration-seconds', '30')
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
  & "$D\px4d.exe" --list | Tee-Object "$LOG\list.txt"
  & "$D\px4d.exe" --list-json | Out-File "$LOG\list.json"
  ```

  daemon起動・ready確認・受信・CARD・停止（X-START/X-LOAD/X-MON/X-CARD-HP/X-STOP相当）。`$ID`は`--device`、
  `$FW`は利用者提供firmware。標準入力をredirectし、`--exit-on-stdin-eof`でcooperative shutdownする。
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

    $captures = @()
    # Q3U4: 0/1/4/5=ISDB-S、2/3/6/7=ISDB-T。8 receiverを同時起動する。他profileはSPEC 10.2.7の該当のみ。
    foreach ($r in 0..7) {
      if ($r -in 0,1,4,5) { $captures += Start-Ts $r 'isdb-s' 1318000 '0' }
      else { $captures += Start-Ts $r 'isdb-t' 527143 '' }
    }
    Wait-Captures $captures
    (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT status 2>> "$LOG\px4ctl.err") | Out-File "$LOG\status-after.txt"

    # CARD（daemon停止前）。抜去/再挿入の物理操作を挟み、各出力とexit値を保存する。
    foreach ($op in 'card-status','card-atr','card-reset') {
      (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT $op 2>> "$LOG\card.err") | Out-File "$LOG\$op.txt"
      "$LASTEXITCODE" | Out-File "$LOG\$op.rc"
    }
    (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT card-apdu 90:30:00:00:00 --repeat 10 2>> "$LOG\card.err") | Out-File "$LOG\card-apdu.txt"
    "$LASTEXITCODE" | Out-File "$LOG\card-apdu.rc"
  } catch {
    $primary = $_
  } finally {
    try { Stop-OwnedProcess $owned 35000 'px4d' | Out-Null }
    catch { if ($null -eq $primary) { $primary = $_ } }
  }
  if ($null -ne $primary) { throw $primary }
  ```

  停止後の確認は所有PIDだけを見る（無関係なglobal processを混ぜない）。実際のexit codeとendpoint残留を
  確認し、空のowned dirだけを削除する:

  ```powershell
  if (-not $owned.Process.HasExited) { throw 'owned px4d is still running after the shutdown attempt' }
  if ($owned.Process.ExitCode -ne 0) { throw "owned px4d exit code was $($owned.Process.ExitCode)" }
  if (Test-Path $RT) {
    $left = Get-ChildItem -Force $RT
    if ($left) { throw "runtime dir still has endpoint/lease entries after cleanup: $($left.Name -join ', ')" }
    Remove-Item $RT -Force   # 空のowned dirだけを削除する。失敗証拠を隠す再帰削除は行わない。
  }
  ```

  CARDの合否は`card-status`の`present=no`、`card-atr`/`card-apdu`の`NO_CARD`（exit 9）、再挿入後の
  ATR/reset/APDU 10回成功をoutput fieldとexit値で確認する。PC/SC adapter・WinSCard経由の確認は行わない（N/A）。

- USB再接続（X-USB-HP相当）: 1 receiver clientを起動し、ユーザーの物理USB切断で`DISCONNECTED`/exit 7と
  client残留なしを確認する。再接続後は`--list`/`--list-json`で再列挙を確認し、旧daemonはX-STOPと同じ
  `Stop-OwnedProcess $owned 35000 'px4d'`でcooperative stopしてから、同じ手順で起動し直す。readyを再確認し、
  短い受信とCARD APDUを再実行する。旧daemonの自動復帰を合格条件にしない。
- 物理操作とsoak: B-CAS/USBの物理操作はユーザーが行い、各操作を依頼する直前にHAOS/SCS側でCodexは`beep`、
  Claude Codeは`vibe`を実行し、依頼から最大5分待つ。LNBは0Vのままとし、別途許可された隔離測定なしに15Vを
  要求・測定しない。5分を超える連続負荷試験は本節に含めず、SPEC 10.5.2「Soak: user decision」に従って
  短時間確認の完了後にユーザーが有無・時間・対象OSを決める。
- 適用条件: Q3U4の項目（8 receiver同時captureを含む）は実際に接続した機種がQ3U4の場合だけ適用し、他profileは
  SPEC 10.2.7の該当項目を使う。TS/counterの合否はSPEC 10.2の適用条件で判定し、shell終了値だけで決めない。
  新しい汎用検証scriptやframeworkは追加しない。
