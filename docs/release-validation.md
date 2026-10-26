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

## 端末nodeと環境状態の退避・復元（T01前）

Termuxの`termux-usb -l`はUSB node path文字列のJSON配列を返す（例: `["/dev/bus/usb/001/004"]`）。配列要素をVID/PID辞書として扱わない。次をbootstrap時と再接続後に毎回実行し、選択結果・一覧をLOGに残す。製品/機種ごとに`TARGET_USB`と`EXPECTED_NODES`を表のUSB IDと当該stage（loader/runtime、単機能/primary+sibling）から設定する。ASICENはstage遷移ごとに期待ID/数を切り替える。0件、JSON型不正、対象一致数の不一致、sysfsで対象pathを対応付けできない場合はpermissionを要求せず停止する。無関係nodeは一覧に記録して対象選択から除外する。

```sh
: "${TARGET_USB:?set model/stage VID:PID, e.g. 3275:0080}"
: "${EXPECTED_NODES:?set exact node count from model/stage}"
case "$TARGET_USB" in [0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]) ;; *) echo "invalid VID:PID" >&2; exit 2;; esac
case "$EXPECTED_NODES" in ''|*[!0-9]*|0) echo "invalid expected node count" >&2; exit 2;; esac
termux-usb -l > "$LOG/termux-usb-paths.json"
SYSFS_ROOT=${SYSFS_ROOT:-/sys/bus/usb/devices}
python - "$LOG/termux-usb-paths.json" "$TARGET_USB" "$EXPECTED_NODES" "$LOG/termux-usb-map.json" "$SYSFS_ROOT" <<'PY'
import glob, json, os, re, sys
src, target, expected, out, sysroot = sys.argv[1:]
paths = json.load(open(src, encoding="utf-8"))
if not isinstance(paths, list) or any(not isinstance(p, str) for p in paths):
    raise SystemExit("termux-usb -l must be a JSON array of USB path strings")
matches, rows = [], []
for path in paths:
    m = re.fullmatch(r"/dev/bus/usb/(\d{3})/(\d{3})", path)
    row = {"path": path, "vid_pid": None, "product": None}
    if m:
        bus, dev = (str(int(v)) for v in m.groups())
        for node in glob.glob(sysroot + "/*"):
            try:
                if open(node + "/busnum").read().strip() == bus and open(node + "/devnum").read().strip() == dev:
                    vid = open(node + "/idVendor").read().strip().lower()
                    pid = open(node + "/idProduct").read().strip().lower()
                    row["vid_pid"] = vid + ":" + pid
                    product = node + "/product"
                    row["product"] = open(product).read().strip() if os.path.exists(product) else ""
                    break
            except OSError:
                continue
    rows.append(row)
    if row["vid_pid"] == target.lower():
        matches.append(path)
json.dump({"api_paths": paths, "mapped": rows, "target": target.lower(), "selected": matches}, open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
if len(matches) != int(expected):
    raise SystemExit(f"expected {expected} {target} node(s), found {len(matches)}; see {out}")
PY
cat "$LOG/termux-usb-map.json"
python3 -c 'import json,sys; [print(p) for p in json.load(open(sys.argv[1]))["selected"]]' "$LOG/termux-usb-map.json" > "$LOG/termux-usb-selected-paths.txt"
```

PX4ではREADMEの選択機種USB IDを`TARGET_USB`にし、PX-Q3U4 (`0511:084a`) は同一筐体の2 nodeを`EXPECTED_NODES=2`、他の単機能機は1にする。

結果が期待数と完全一致した場合だけ、JSONの`selected`に記録された各pathへ`termux-usb -r <path>`（または製品固有の`-r -e`）を実行する。USB permission後にdaemonを開始する。再接続時は古いpath/FDを使わず、同じ列挙・sysfs照合を再実行する。sysfs未読・node未対応・無許可候補複数は合格ではなく未達として記録する。

正式検証でE02/E03/E07等により環境状態を変更する場合は、ユーザー承認済みの対象・値・影響範囲を確定し、保護されたrun専用LOGと復元可能な状態保存を準備してから実行する。適用後に必要な試験を行い、終了時に保存値へ戻してreadback/比較する。承認範囲を超える変更やHA Core/mirakcの再起動は行わない。

- **E02 HAOS/Supervisor options:** `/addons/self/info`の`options`は対象addon自身のapp recordに保存されたraw optionsで、`/options/config`のdefaults・secret解決後の値とは異なる。Supervisor APIの実装は[apps.py](https://github.com/home-assistant/supervisor/blob/main/supervisor/api/apps.py#L1620-L1700)および[options endpoints](https://github.com/home-assistant/supervisor/blob/main/supervisor/api/apps.py#L1832-L2015)を参照。対象addon内で認可済みtokenを使い、raw表現はmode 600のprivate backupにだけ保存する。値を画面・共有log・evidenceへ出さない。validateの`valid`がtrue以外、または読み出し/HTTP/JSON処理が失敗した場合はoptions POST前に終了する。承認済み範囲内で対象addonの起動・停止とoptions復元/比較を行い、HA Core/mirakcは再起動しない。
  ```sh
  set -eu
  umask 077
  mkdir -m 700 -p "$LOG/private"
  : "${SUPERVISOR_TOKEN:?run inside the target add-on with its token}"
  API=http://supervisor/addons/self
  curl -fsS -H "Authorization: Bearer $SUPERVISOR_TOKEN" "$API/info" -o "$LOG/private/info.before.json"
  python3 - "$LOG/private/info.before.json" "$LOG/private/options.before.json" <<'PY'
  import json, sys
  x=json.load(open(sys.argv[1], encoding="utf-8")); x=x.get("data", x)
  options=x.get("options")
  if not isinstance(options, dict): raise SystemExit("raw app options unavailable")
  json.dump(options, open(sys.argv[2], "w", encoding="utf-8"), sort_keys=True)
  PY
  chmod 600 "$LOG/private/"*.json
  cp "$LOG/private/options.before.json" "$LOG/private/options.restore.json"
  cp "$LOG/private/options.before.json" "$LOG/private/options.trial.json"
  : "${TRIAL_OPTION_KEY:?set one approved non-secret top-level option key}"
  : "${TRIAL_OPTION_JSON:?set its approved JSON value}"
  python3 - "$LOG/private/options.trial.json" "$TRIAL_OPTION_KEY" "$TRIAL_OPTION_JSON" <<'PY'
  import json, sys
  p, key, value=sys.argv[1:]
  options=json.load(open(p, encoding="utf-8"))
  if key not in options: raise SystemExit("trial key is not an existing option")
  options[key]=json.loads(value)
  json.dump(options, open(p, "w", encoding="utf-8"), sort_keys=True)
  PY
  python3 -m json.tool "$LOG/private/options.trial.json" > /dev/null
  python3 - "$LOG/private/options.trial.json" "$LOG/private/options-trial.request.json" <<'PY'
  import json, sys
  json.dump({"options":json.load(open(sys.argv[1], encoding="utf-8"))}, open(sys.argv[2], "w", encoding="utf-8"))
  PY
  # /options/validate accepts the raw options object; only /options uses the {"options": ...} request schema.
  cp "$LOG/private/options.trial.json" "$LOG/private/options-trial.validate.json"
  chmod 600 "$LOG/private/"*.json
  curl -fsS -H "Authorization: Bearer $SUPERVISOR_TOKEN" -H 'Content-Type: application/json' --data-binary @"$LOG/private/options-trial.validate.json" "$API/options/validate" -o "$LOG/private/options-trial.validation-response.json"
  if ! python3 -c 'import json,sys; x=json.load(open(sys.argv[1])); x=x.get("data",x); sys.exit(0 if x.get("valid") is True else 1)' "$LOG/private/options-trial.validation-response.json"; then echo 'options validation rejected; no trial POST was sent' >&2; exit 1; fi
  curl -fsS -H "Authorization: Bearer $SUPERVISOR_TOKEN" -H 'Content-Type: application/json' --data-binary @"$LOG/private/options-trial.request.json" "$API/options" -o "$LOG/private/options-trial.post.json"
  ```
  run終了後、保存したraw optionsを同じ`/options` endpointへ戻し、`/info`のraw `options`とprivate backupの完全一致を確認する。`/options/config`のvalidated値との比較で復元を判定しない。
  ```sh
  set -eu
  python3 - "$LOG/private/options.restore.json" "$LOG/private/options-restore.request.json" <<'PY'
  import json, sys
  json.dump({"options":json.load(open(sys.argv[1], encoding="utf-8"))}, open(sys.argv[2], "w", encoding="utf-8"))
  PY
  curl -fsS -H "Authorization: Bearer $SUPERVISOR_TOKEN" -H 'Content-Type: application/json' --data-binary @"$LOG/private/options-restore.request.json" "$API/options" -o "$LOG/private/options-restore.post.json"
  curl -fsS -H "Authorization: Bearer $SUPERVISOR_TOKEN" "$API/info" -o "$LOG/private/info.after.json"
  python3 - "$LOG/private/info.after.json" "$LOG/private/options.after.json" <<'PY'
  import json, sys
  x=json.load(open(sys.argv[1], encoding="utf-8")); x=x.get("data", x)
  json.dump(x.get("options"), open(sys.argv[2], "w", encoding="utf-8"), sort_keys=True)
  PY
  cmp "$LOG/private/options.restore.json" "$LOG/private/options.after.json"
  ```


optionsを適用した後と元値へ戻した後に、それぞれ別の認可済みHAOS CLI sessionから対象add-onだけを再起動する。HA Core/mirakcは再起動しない。

```sh
ADDON_SLUG='<target-addon-slug>'
ha addons restart "$ADDON_SLUG"
```

- **E03 Linux module/udev/group:**対象udev rule fileの元内容/不在状態、対象userのsupplementary groups、対象USB nodeのmode/UID/GIDを保存する。`DEVICE_NODE`と`TARGET_USB`は同一候補機器の実値に限定し、sysfs pathとVID/PIDが一致しない場合は何も適用せず停止する。rule再読込時のtriggerはそのsysfs device pathだけへ指定する。変更は別途承認された試験でのみ実施する。
  ```sh
  set -eu
  TEST_USER='<non-root-test-user>'; LOG='<private-run-log>'
  DEVICE_NODE='/dev/bus/usb/BBB/AAA'; TARGET_USB='<selected-vid>:<selected-pid>'
  for f in /etc/modprobe.d/smsusb-blacklist.conf /etc/udev/rules.d/99-usb-access.rules; do
    n=$(basename "$f")
    if [ -e "$f" ]; then cp -a "$f" "$LOG/$n.before"; printf 'present\n' > "$LOG/$n.state"; else printf 'absent\n' > "$LOG/$n.state"; fi
  done
  id "$TEST_USER" > "$LOG/test-user.before.txt"; PRIMARY_GROUP=$(id -gn "$TEST_USER")
  id -nG "$TEST_USER" | python3 -c 'import sys; p=sys.argv[1]; print(",".join(g for g in sys.stdin.read().split() if g != p))' "$PRIMARY_GROUP" > "$LOG/groups.before.txt"
  test -c "$DEVICE_NODE"
  SYSFS_PATH=$(udevadm info --query=path --name="$DEVICE_NODE")
  SYSFS_DEVICE="/sys$SYSFS_PATH"
  test -f "$SYSFS_DEVICE/idVendor" -a -f "$SYSFS_DEVICE/idProduct"
  ACTUAL_USB="$(cat "$SYSFS_DEVICE/idVendor"):$(cat "$SYSFS_DEVICE/idProduct")"
  test "$(printf '%s' "$ACTUAL_USB" | tr '[:upper:]' '[:lower:]')" = "$(printf '%s' "$TARGET_USB" | tr '[:upper:]' '[:lower:]')"
  stat -c '%a %u %g' "$DEVICE_NODE" > "$LOG/usb-node.before.txt"
  printf '%s\n' "$SYSFS_PATH" > "$LOG/usb-sysfs-path.before.txt"
  # 承認済みrunでのみ適用する。適用は隔離subshell内で行い、途中失敗でも親shellの復元へ戻る:
  apply_trial_access() (
    set -eu
    : "${BLACKLIST_MODULES:?set only the modules required by the selected product/model}"
    : > /etc/modprobe.d/smsusb-blacklist.conf || exit $?
    for module in $BLACKLIST_MODULES; do printf 'blacklist %s\n' "$module" >> /etc/modprobe.d/smsusb-blacklist.conf || exit $?; done
    VID=${TARGET_USB%:*}; PID=${TARGET_USB#*:}
    printf 'SUBSYSTEM=="usb", ATTR{idVendor}=="%s", ATTR{idProduct}=="%s", MODE="0660", GROUP="video"\n' "$VID" "$PID" > /etc/udev/rules.d/99-usb-access.rules || exit $?
    udevadm control --reload-rules || exit $?
    udevadm trigger --action=change "$SYSFS_DEVICE" || exit $?
    udevadm settle || exit $?
    usermod -aG video "$TEST_USER" || exit $?
  )
  if apply_trial_access; then APPLY_RC=0; else APPLY_RC=$?; fi
  printf '%s\n' "$APPLY_RC" > "$LOG/access-apply.rc"
  RESTORE_RC=0
  set +e
  for f in /etc/modprobe.d/smsusb-blacklist.conf /etc/udev/rules.d/99-usb-access.rules; do
    n=$(basename "$f")
    if [ "$(cat "$LOG/$n.state")" = present ]; then
      cp -a "$LOG/$n.before" "$f" || RESTORE_RC=1
      cmp "$LOG/$n.before" "$f" || RESTORE_RC=1
    else
      rm -f "$f" || RESTORE_RC=1
      test ! -e "$f" || RESTORE_RC=1
    fi
  done
  udevadm control --reload-rules || RESTORE_RC=1
  if test -c "$DEVICE_NODE" && test "$(udevadm info --query=path --name="$DEVICE_NODE")" = "$SYSFS_PATH" && test "$(cat "$SYSFS_DEVICE/idVendor"):$(cat "$SYSFS_DEVICE/idProduct")" = "$ACTUAL_USB"; then :; else RESTORE_RC=1; fi
  udevadm trigger --action=change "$SYSFS_DEVICE" || RESTORE_RC=1
  udevadm settle || RESTORE_RC=1
  if set -- $(cat "$LOG/usb-node.before.txt") && test "$#" -eq 3; then
    chown "$2:$3" "$DEVICE_NODE" || RESTORE_RC=1
    chmod "$1" "$DEVICE_NODE" || RESTORE_RC=1
  else RESTORE_RC=1; fi
  if stat -c '%a %u %g' "$DEVICE_NODE" > "$LOG/usb-node.after.txt"; then cmp "$LOG/usb-node.before.txt" "$LOG/usb-node.after.txt" || RESTORE_RC=1; else RESTORE_RC=1; fi
  if GROUPS_BEFORE=$(cat "$LOG/groups.before.txt"); then
    if [ -n "$GROUPS_BEFORE" ]; then usermod -G "$GROUPS_BEFORE" "$TEST_USER" || RESTORE_RC=1; else usermod -G '' "$TEST_USER" || RESTORE_RC=1; fi
    id "$TEST_USER" > "$LOG/test-user.after.txt" || RESTORE_RC=1
    if PRIMARY_AFTER=$(id -gn "$TEST_USER") && id -nG "$TEST_USER" | python3 -c 'import sys; p=sys.argv[1]; print(",".join(g for g in sys.stdin.read().split() if g != p))' "$PRIMARY_AFTER" > "$LOG/groups.after.txt"; then cmp "$LOG/groups.before.txt" "$LOG/groups.after.txt" || RESTORE_RC=1; else RESTORE_RC=1; fi
  else RESTORE_RC=1; fi
  printf '%s\n' "$RESTORE_RC" > "$LOG/access-restore.rc"
  set -e
  if [ "$APPLY_RC" -ne 0 ]; then exit "$APPLY_RC"; fi
  if [ "$RESTORE_RC" -ne 0 ]; then echo 'access state restoration incomplete; retain evidence and stop' >&2; exit "$RESTORE_RC"; fi
  ```

- **E07 Android stay-awake:**初回ADB readが非0/空/不正値なら書込み前に停止する。`null`は未設定、十進整数は保存値として区別する。権限・readback失敗を無視しない。正式検証では承認済み端末で設定を適用し、試験後に保存値へ戻してreadback/比較する。synthetic fixtureは局所確認に限り、実端末での検証を代替しない。
  ```sh
  set -eu
  LOG='<private-run-log>'
  if ! BEFORE=$(adb shell settings get global stay_on_while_plugged_in); then echo 'initial Android settings read failed; no write attempted' >&2; exit 1; fi
  BEFORE=$(printf '%s' "$BEFORE" | tr -d '\r')
  case "$BEFORE" in null) printf 'absent\n' > "$LOG/stay-awake.before-state.txt";; ''|*[!0-9]*) echo 'initial Android settings value is invalid; no write attempted' >&2; exit 1;; *) printf 'present\n' > "$LOG/stay-awake.before-state.txt";; esac
  printf '%s\n' "$BEFORE" > "$LOG/stay-awake.before.txt"
  : "${TRIAL_VALUE:?set an approved decimal test value}"
  case "$TRIAL_VALUE" in ''|*[!0-9]*) echo 'trial value must be decimal' >&2; exit 2;; esac
  if adb shell settings put global stay_on_while_plugged_in "$TRIAL_VALUE"; then TRIAL_RC=0; else TRIAL_RC=$?; fi
  if [ "$BEFORE" = null ]; then adb shell settings delete global stay_on_while_plugged_in; else adb shell settings put global stay_on_while_plugged_in "$BEFORE"; fi
  if ! AFTER=$(adb shell settings get global stay_on_while_plugged_in); then echo 'Android settings readback failed' >&2; exit 1; fi
  AFTER=$(printf '%s' "$AFTER" | tr -d '\r')
  case "$AFTER" in null) printf 'absent\n' > "$LOG/stay-awake.after-state.txt";; ''|*[!0-9]*) echo 'Android settings readback is invalid' >&2; exit 1;; *) printf 'present\n' > "$LOG/stay-awake.after-state.txt";; esac
  printf '%s\n' "$AFTER" > "$LOG/stay-awake.after.txt"
  cmp "$LOG/stay-awake.before-state.txt" "$LOG/stay-awake.after-state.txt"
  cmp "$LOG/stay-awake.before.txt" "$LOG/stay-awake.after.txt"
  test "$TRIAL_RC" -eq 0
  ```

## 共通試験シナリオ T00–T14

### run別candidateの取得と作業directory

候補のsource commit SHAはこの手順書のcommit SHAと同一である必要はない。GitHub CLIで対象repoの成功した`build_userland.yml` runを列挙し、版別記録のcandidate source SHAと一致するrunだけを選ぶ。runのattempt、head SHA、結論、artifact ID/name/expiryを記録し、未成功・期限切れ・source不一致は未達として停止する。手動dispatch、build、正式Release workflowはこの手順では実行しない。

Linux/macOSでは候補確認hostにGitHub CLI、Python 3.11以上、`sha256sum`（macOSは`shasum`）、`tar`を用意する。TermuxではAndroid側へTermux:API companion appをインストールし、Termux内で`pkg install gh python coreutils tar termux-api util-linux`を実行して`termux-usb`/`setsid`を用意する。package導入はhost owner承認後に行う。WindowsではGitHub CLIとPython 3.11以上を用意する。

```sh
set -eu
gh run list --repo Khronos31/px4-userland --workflow build_userland.yml --limit 20
RUN_ID='<successful-candidate-run-id>'; TARGET='<native-target>'
RUN_JSON=$(gh run view "$RUN_ID" --repo Khronos31/px4-userland --json headSha,attempt,conclusion)
SOURCE_SHA=$(printf '%s\n' "$RUN_JSON" | python3 -c 'import json,sys; print(json.load(sys.stdin)["headSha"])')
printf '%s\n' "$RUN_JSON" | python3 -c 'import json,sys; r=json.load(sys.stdin); assert r["conclusion"] == "success" and r["headSha"] and r["attempt"] > 0'
test -n "$SOURCE_SHA"
gh api "repos/Khronos31/px4-userland/actions/runs/$RUN_ID/artifacts" --jq '.artifacts[] | [.id,.name,.expired] | @tsv'
WORK=$(mktemp -d "${TMPDIR:-$HOME}/px4-run-${RUN_ID}.XXXXXX"); chmod 700 "$WORK"; mkdir -m 700 "$WORK/download"
LOG="$WORK/log"; mkdir -m 700 "$LOG"
if [ -n "${PREFIX:-}" ]; then
  command -v termux-usb >/dev/null
  command -v setsid >/dev/null
  termux-usb -l > "$LOG/termux-usb-prerequisite.json"
  python3 -c 'import json,sys; x=json.load(open(sys.argv[1])); assert isinstance(x,list) and all(isinstance(p,str) for p in x)' "$LOG/termux-usb-prerequisite.json"
fi
gh auth status
gh run download "$RUN_ID" --repo Khronos31/px4-userland --name release-candidate --dir "$WORK/download"
cd "$WORK/download"; sha256sum -c SHA256SUMS || shasum -a 256 -c SHA256SUMS
ARCHIVE="px4-userland-<version>-$TARGET.tar.gz"; mkdir "$WORK/candidate"
tar -xzf "$WORK/download/$ARCHIVE" -C "$WORK/candidate"; D="$WORK/candidate"
LOG="$WORK/log"; RT="$WORK/runtime"; mkdir -m 700 -p "$LOG" "$RT"; cd "$D"; export PATH="$D:$PATH"
test -x "$D/px4d"; test -x "$D/px4-ts"; test -x "$D/px4ctl"
python3 -c 'import json,sys; m=json.load(open("manifest.json")); assert m.get("source_ref")==sys.argv[1] and m.get("platform")==sys.argv[2]' "$SOURCE_SHA" "$TARGET"
```

Windowsの`windows-x86_64` ZIPも通常配布対象のarchiveである。同じ成功runにある`release-candidate` artifactのID/nameを記録して取得し、下記のWindows手順で検証する。

```powershell
$ErrorActionPreference = 'Stop'
$RunId = '<successful-candidate-run-id>'; $Target = 'windows-x86_64'
$Work = Join-Path $HOME "px4-run-$RunId"; $Download = Join-Path $Work 'download'; $D = Join-Path $Work 'candidate'
if (Test-Path $Work) { throw "refusing to reuse $Work" }
New-Item -ItemType Directory -Path $Download,$D,(Join-Path $Work 'log'),(Join-Path $Work 'runtime') | Out-Null
gh run view $RunId --repo Khronos31/px4-userland --json headSha,attempt,conclusion
gh api "repos/Khronos31/px4-userland/actions/runs/$RunId/artifacts" --jq '.artifacts[] | [.id,.name,.expired] | @tsv'
gh run download $RunId --repo Khronos31/px4-userland --name release-candidate --dir $Download
$ZIP = Get-ChildItem $Download -Filter '*-windows-x86_64.zip' | Select-Object -First 1
if (-not $ZIP) { throw 'candidate ZIP missing' }
$run = gh run view $RunId --repo Khronos31/px4-userland --json headSha,attempt,conclusion | ConvertFrom-Json
if ($run.conclusion -ne 'success') { throw 'candidate run is not successful' }
$CandidateSourceSha = $run.headSha
$sum = Get-Content (Join-Path $Download 'SHA256SUMS') | Where-Object { $_ -match "  $([regex]::Escape($ZIP.Name))$" }
if (-not $sum) { throw 'candidate ZIP checksum entry missing' }
$expected = ($sum -split '\s+')[0]; $actual = (Get-FileHash -Algorithm SHA256 $ZIP.FullName).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw 'candidate ZIP checksum mismatch' }
Expand-Archive -LiteralPath $ZIP.FullName -DestinationPath $D
$manifest = Get-Content (Join-Path $D 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.source_ref -ne $CandidateSourceSha -or $manifest.platform -ne $Target) { throw 'candidate provenance/target mismatch' }
$LOG = Join-Path $Work 'log'; $RT = Join-Path $Work 'runtime'; Set-Location $D
```

TermuxはOSの`/tmp`を使わず`$PREFIX/tmp`へrun directoryを作る。`termux-usb -l`の各pathを保存し、VID/PID/product nameが対象機器と一致したpathだけを`termux-usb -r <path>`で許可する。候補取得後は`$D`/`$LOG`/`$RT`をTermux内の書込可能な`$HOME`配下へ置く。非対象nodeへpermission requestしない。候補のmanifest/source SHAとarchive hashを確定するまでUSB操作へ進まない。

E02/E03/E15-M等のOS権限不足はまず`id`, `stat`, `lsusb -t`（Termuxでは`termux-usb -l`）で読み取り記録し停止する。udev ruleやgroup変更が別途承認された場合は変更前の対象file/ACLを`$LOG`へ保存し、承認されたruleだけ適用する。試験後に保存値を復元して差分がないことを確認する。HAOS/Supervisor optionsは既存の承認・退避・復元手順が完了するまで変更しない。

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

OS/ABI別の具体操作は [正式アーカイブの環境別手順](#px4-os-t07-recipes) を参照する。

### T08 通常停止・cleanup

- **適用対象/頻度:** 全test終了時。
- **準備:** 各owned client/daemon PIDとruntimeを記録してから起動する。
- **実操作/利用者への依頼:** §0.1 X-STOP/X-RESIDでclient wait、daemon EOF/SIGINT、owned PID cleanup。
- **期待値/終了値:** 全owned process終了、TS close、FD/IPC/socket/runtime残留なし。timeoutはfail。
- **停止/cleanup:** 無関係processへsignalしない。test専用runtimeだけ片付け失敗log保持。
- **記録:** PID/role/exit、process/FD/IPC/socket/runtime、復旧状態。

### 共通helperの固定取得（T09前提）

T09を実行する前に、候補sourceとは独立した共通runbook helperをSiano runbookのimmutable commit `c2378ea3c52b3e415ec6c7acfea4b8f758b46eda`から取得する。candidate checkout/archive内のhelper有無に依存しない。POSIX/Termuxでは次を一度実行し、以後の全captureで`$TOOL/ts-check.py`を使う。

```sh
RUNBOOK_REF='c2378ea3c52b3e415ec6c7acfea4b8f758b46eda'; HELPER_SHA256='dbb4826e3eb4f0cb13850b59864c7219b8929822976c9e213d3948edbe450f05'
TOOL="$WORK/tooling"; mkdir -m 700 -p "$TOOL"
gh api -H 'Accept: application/vnd.github.raw' "repos/Khronos31/siano-userland/contents/docs/release-validation/ts-check.py?ref=$RUNBOOK_REF" > "$TOOL/ts-check.py"
if command -v sha256sum >/dev/null 2>&1; then printf '%s  %s\n' "$HELPER_SHA256" "$TOOL/ts-check.py" | sha256sum -c -; else printf '%s  %s\n' "$HELPER_SHA256" "$TOOL/ts-check.py" | shasum -a 256 -c -; fi
printf '%s\n' "$RUNBOOK_REF $HELPER_SHA256" > "$LOG/ts-helper-version.txt"
```

Windows PowerShell 5.1でも同じ固定版を新しいtooling directoryへ取得し、hash一致後にのみ使う。

```powershell
$RunbookRef = 'c2378ea3c52b3e415ec6c7acfea4b8f758b46eda'; $HelperSha256 = 'dbb4826e3eb4f0cb13850b59864c7219b8929822976c9e213d3948edbe450f05'
$Tool = Join-Path $Work 'tooling'; New-Item -ItemType Directory -Path $Tool | Out-Null
$Helper = Join-Path $Tool 'ts-check.py'
Invoke-WebRequest -UseBasicParsing -Uri "https://raw.githubusercontent.com/Khronos31/siano-userland/$RunbookRef/docs/release-validation/ts-check.py" -OutFile $Helper
$Actual = (Get-FileHash -LiteralPath $Helper -Algorithm SHA256).Hash.ToLowerInvariant()
if ($Actual -ne $HelperSha256) { throw 'runbook TS helper hash mismatch' }
"$RunbookRef $Actual" | Set-Content (Join-Path $LOG 'ts-helper-version.txt')
```

helperのversionはrunbook Git commitとSHA-256の組で固定する。candidate source SHA、artifact digest、helper ref/hashを別々に記録する。

### T09 TS・counter・終了値判定

- **適用対象/頻度:** 全capture/soak。
- **準備:** Python 3.11以上を`python3 --version`で確認し、固定取得した`$TOOL/ts-check.py`（Windowsは`$Helper`）を使う。候補ごとに新規作成した`$LOG`を使う。
- **実操作/利用者への依頼:** POSIXでは各TSに`python3 $TOOL/ts-check.py "$LOG/<receiver>.ts" > "$LOG/<receiver>.ts-check.json"`を実行する。Windowsは同じhelperを`python $Helper "$LOG\<receiver>.ts"`で実行し、標準出力を`$LOG`へ保存する。全capture/soakの各TSで実行し、候補daemonのdrop/USB counterも記録する。
- **期待値/終了値:** null PIDはcontinuity判定から除外する。同一PIDの隣接payload duplicate pairはPCR timestamp以外の全byte一致時だけ1組を許容し、duplicate indexをJSONへ記録する。3個目、内容差、非連続再送はcontinuity error。retune境界を確認できるrunはzero-basedの最初の出力packet indexを`--retune-boundary-at`へ渡し、境界後の各PID初回packetだけCC比較を除外する。TEI、sync、framing、invalid adaptationは除外しない。PCR reserved bits/extension違反とinvalid adaptationはfail。全TSが製品SPEC基準を満たし通常取得exit 0。非空file/counter 0だけでpassにしない。
- **停止/cleanup:** 元TS/hash/logを残してT08 cleanup。
- **記録:** `python3 --version`、helperのGit SHA/hash、TS hash、helperのJSON/終了値、duplicate/null/invalid packet index、sync/TEI/continuity/drop/counter、client exit。

Windowsで各TSを確認するcommand:

```powershell
$helper = Join-Path $Tool 'ts-check.py'; $ts = Join-Path $LOG '<receiver>.ts'
$json = Join-Path $LOG '<receiver>.ts-check.json'; $err = Join-Path $LOG '<receiver>.ts-check.err'
& python $helper $ts 1> $json 2> $err; $parserRc = $LASTEXITCODE
$parserRc | Set-Content (Join-Path $LOG '<receiver>.ts-check.rc')
if ($parserRc -ne 0) { throw "TS parser failed: $parserRc" }
```

### T10 native PC/SC・WinSCard

- **適用対象/頻度:** 対応OS/APIのみ。非対応製品は理由付きN/A。
- **準備:** native OS adapter/consumerとcandidate IFD/DLL pathを用意。
- **実操作/利用者への依頼:** Linux native IFDは「Linux native PC/SC（E02/E03/E15）」、macOS E04 [native PC/SC](#e04-macos-native-pcsc)、Windows §6 E17/Windows WinSCard recipeでcandidate DLL/bundle、consumer、reader/card transactionを確認。
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

各正式archive/E-IDで適用されるT00–T14を独立して実行する。試験項目の準備、操作、期待値/終了値、停止/cleanup、記録は「共通試験シナリオ T00–T14」を唯一の定義とし、下表はarchiveごとのOS/ABI環境手順への参照を示す。異なるarchive、libc、OS/ABI、USB access boundaryの結果で代替しない。機能対象外はT本文と製品固有条件に理由を記し、未実施・unsupported・候補未入手・環境不足は未達として記録する。

| archive / E-ID / 実行環境とprocedure |
|---|
| `linux-glibc-x86_64` / [E03](#e03-anduinos-x86_64基準linux-x86_64) / Linux x86_64/glibc |
| `linux-musl-x86_64` / [E02](#e02-haos-x86_64-alpinemusl-add-on基準環境supervisor) / Linux x86_64/musl |
| `linux-glibc-aarch64` / [E15-G](#e15-g--e15-m-linux-aarch64glibc--musl-native確認環境) / Linux aarch64/glibc |
| `linux-musl-aarch64` / [E15-M](#e15-g--e15-m-linux-aarch64glibc--musl-native確認環境) / Linux aarch64/musl |
| `darwin-arm64` / [E04](#e04-macos-arm64基準darwin環境) / macOS arm64 |
| `android-aarch64` / [E05](#e05-android-termux-aarch64基準android-aarch642-fd) / Android Termux/aarch64 |
| `android-armv7a` / [E06](#e06-android-termux-armv7a基準android-armv7a2-fd) / Android Termux/armv7a |
| `android-x86_64` / [E07](#e07-bliss-os-x86_64-termux基準android-x86_642-fdnative確認環境例) / Android Termux/x86_64 |
| `windows-x86_64` / [E17](#e17-windows-11-x86_64windowsアーカイブのnative確認phase-12) / Windows 11 x86_64 |

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

| 配布binary archive | 環境例 | 各配布archiveで毎release必須の実機操作 |
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

各配布archiveの実機確認は列挙、短い受信、カード状態確認、B-CAS抜去/再挿入、USB切断/再接続、復旧確認を順に実行する。これら一連の確認に総時間上限を設けない。5分の上限はユーザーへ物理操作を依頼してから操作が完了するまでの待機だけに適用する。候補archiveのchecksum・展開、APDU、RF/電源/firmware、host準備は検証開始前に済ませる。5分以内に物理操作が行われなければ、その操作を未完了として記録し、物理deviceを使う工程を中断する。頻度表で毎release必須とする代表機2時間soakは実施する。これとは別の5分を超える追加連続負荷試験は、ユーザーが決定するsoakへ分ける。

canonical runtime は、そのartifactの実機確認を代表する環境を指す。同一executableを共有するruntimeでもaccess pathが異なれば、証拠は自動継承しない。

### 0.1 共通コマンド

artifact は候補 commit の `release-candidate`（§2）だけを使い、手元 build や旧 archive で代用しない。以下の変数は同じ検証端末で一度設定する。山括弧の例示値はshellへ入力せず、実値へ置き換える。値が空または未置換ならdaemon/clientを起動する前に停止する。

artifact 検証と展開（X-PREP）:

```sh
set -eu
ARCHIVE='px4-userland-1.0.0-linux-glibc-x86_64.tar.gz' # 候補artifact名を設定
FW='/absolute/path/to/user-provided-model-firmware.bin' # PX4 archiveにはfirmwareを同梱しない
ID='usb:1-2.3'                                         # --list-jsonで確認した実在位置
INSTANCE='candidate-q3u4'                              # この試験用のinstance名
RUN_ID='rc-20261026-q3u4'                              # 記録に使う一意な名前
FW_SHA256='5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484'
require_value() { name=$1 value=$2; case "$value" in ''|*'<'*|*'>'*) echo "invalid or unreplaced $name" >&2; return 1 ;; esac; }
require_value ARCHIVE "$ARCHIVE"; require_value FW "$FW"; require_value ID "$ID"
require_value INSTANCE "$INSTANCE"; require_value RUN_ID "$RUN_ID"
sha256sum -c SHA256SUMS                              # macOS: shasum -a 256 -c
D=$(mktemp -d); RT=$(mktemp -d "${TMPDIR:-$HOME}/px4-userland-${RUN_ID}.XXXXXX")
LOG=$(mktemp -d "${TMPDIR:-$HOME}/px4-logs-${RUN_ID}.XXXXXX"); chmod 700 "$D" "$RT" "$LOG"
tar -xzf "$ARCHIVE" -C "$D"
test -s "$FW"
if [ "$(uname -s)" = Darwin ]; then printf '%s  %s\n' "$FW_SHA256" "$FW" | shasum -a 256 -c -; else printf '%s  %s\n' "$FW_SHA256" "$FW" | sha256sum -c -; fi
"$D/px4d" --list-json > "$LOG/discovery.json"         # enclosures[].devices[]とreceivers[]を確認
python3 - "$LOG/discovery.json" "${ID#usb:}" <<'PY'
import json,sys
root=json.load(open(sys.argv[1],encoding='utf-8'))
def has_port(value):
    if isinstance(value,dict): return value.get('port') == sys.argv[2] or any(has_port(v) for v in value.values())
    if isinstance(value,list): return any(has_port(v) for v in value)
    return False
if not has_port(root): raise SystemExit('ID port absent from discovery JSON')
PY
test -x "$D/px4d"; test -x "$D/px4-ts"; test -x "$D/px4ctl"
```

daemon 起動と準備完了確認（X-START）:

X-PREP、X-START、X-LOAD/X-MON、X-STOP/X-RESIDは同じ対話shellで順に実行する。X-STARTの`EXIT` trapはそのshellを閉じるまでdaemonを保持するため、fenceを別scriptとして実行しない。

```sh
"$D/px4d" --device "$ID" --instance "$INSTANCE" --firmware "$FW" --runtime-dir "$RT" > "$LOG/px4d.out" 2> "$LOG/px4d.err" &
P=$!
cleanup_px4d() {
  original=$?; trap - EXIT
  if kill -0 "$P" 2>/dev/null; then kill -TERM "$P"; fi
  if wait "$P"; then daemon_rc=0; else daemon_rc=$?; fi
  echo "$daemon_rc" > "$LOG/px4d.rc"
  if [ "$original" -eq 0 ] && [ "$daemon_rc" -ne 0 ]; then original=$daemon_rc; fi
  exit "$original"
}
trap cleanup_px4d EXIT
# statusを最大60秒再試行し、readyを確認する。group modeならpx4d/px4ctlの両方に --group を付ける。
i=0; ready=0
while [ "$i" -lt 60 ]; do
  if "$D/px4ctl" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" status > "$LOG/status-start.txt" 2> "$LOG/status-start.err" && grep -q 'ready' "$LOG/status-start.txt"; then ready=1; break; fi
  kill -0 "$P" || { echo 'px4d exited before ready' >&2; exit 1; }
  i=$((i + 1)); sleep 1
done
test "$ready" -eq 1 || { echo 'px4d did not become ready' >&2; exit 1; }
"$D/px4ctl" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" list > "$LOG/ctl-list.txt" 2> "$LOG/ctl-list.err"
```

受信と監視（X-LOAD / X-MON）:

USB device/fdを開くdaemonには対応するselectorが必要で、`--instance`はdaemon endpoint namespaceを指定する。daemonの既存USB claim/namespace lockが排他を担う。instance付きclientは既存endpointへ接続し、`--device`は省略できるが、その場合device identity照合は行わない。clientにも`--device`を渡す場合はdaemonと同じselectorを使い、identity assertionとして照合させる。instanceとruntime directoryもdaemonに合わせる。以下の単体受信と8 receiver例ではselectorを明示する。短い単一receiver確認には次の形を使い、durationは該当版matrix値を設定する。

```sh
set -eu
RECEIVER='0'; CAPTURE_SECONDS='10'
require_value RECEIVER "$RECEIVER"; require_value CAPTURE_SECONDS "$CAPTURE_SECONDS"
case "$RECEIVER:$CAPTURE_SECONDS" in *[!0-9:]*|:*) echo 'receiver/duration must be numeric' >&2; exit 1 ;; esac
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --receiver "$RECEIVER" --channel T27 \
  --runtime-dir "$RT" --output "$LOG/t27.ts" --time "$CAPTURE_SECONDS" > "$LOG/t27.out" 2> "$LOG/t27.err" || { rc=$?; echo "$rc" > "$LOG/t27.rc"; exit "$rc"; }
echo 0 > "$LOG/t27.rc"
```

代表機の必須2時間runは同じactive candidate daemonの下で、短時間smokeと別に次を実行する。初回/majorのmacOS・WindowsではCAPTURE_SECONDS=1800、他OS/ABIでは600を設定する。

```sh
CAPTURE_SECONDS='7200'
require_value CAPTURE_SECONDS "$CAPTURE_SECONDS"
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --receiver "$RECEIVER" --channel T27 \
  --runtime-dir "$RT" --output "$LOG/representative-soak.ts" --time "$CAPTURE_SECONDS" \
  > "$LOG/representative-soak.out" 2> "$LOG/representative-soak.err" || { rc=$?; echo "$rc" > "$LOG/representative-soak.rc"; exit "$rc"; }
echo 0 > "$LOG/representative-soak.rc"
```

Q3U4の8 receiver確認は次の8コマンドを同じ端末で個別に実行する。各receiverは30秒で終了する。これは受信確認であり、物理操作の応答を待つ間の負荷継続には使わない。

```sh
set -eu
# Q3U4 8 receiver（受信継続: 0/1/4/5=S、2/3/6/7=T）
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 0 --channel BS15_0 --lnb-voltage 0 --time 30 --output "$LOG/r0.ts" > "$LOG/r0.out" 2> "$LOG/r0.err" & R0=$!
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 1 --channel BS15_0 --lnb-voltage 0 --time 30 --output "$LOG/r1.ts" > "$LOG/r1.out" 2> "$LOG/r1.err" & R1=$!
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 4 --channel BS15_0 --lnb-voltage 0 --time 30 --output "$LOG/r4.ts" > "$LOG/r4.out" 2> "$LOG/r4.err" & R4=$!
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 5 --channel BS15_0 --lnb-voltage 0 --time 30 --output "$LOG/r5.ts" > "$LOG/r5.out" 2> "$LOG/r5.err" & R5=$!
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 2 --system isdb-t --freq 527143000 --time 30 --output "$LOG/r2.ts" > "$LOG/r2.out" 2> "$LOG/r2.err" & R2=$!
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 3 --system isdb-t --freq 527143000 --time 30 --output "$LOG/r3.ts" > "$LOG/r3.out" 2> "$LOG/r3.err" & R3=$!
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 6 --system isdb-t --freq 527143000 --time 30 --output "$LOG/r6.ts" > "$LOG/r6.out" 2> "$LOG/r6.err" & R6=$!
"$D/px4-ts" --device "$ID" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 7 --system isdb-t --freq 527143000 --time 30 --output "$LOG/r7.ts" > "$LOG/r7.out" 2> "$LOG/r7.err" & R7=$!
# 8個それぞれの終了値を記録する（各waitは個別に実行）。
FAIL=0
for entry in "$R0:r0" "$R1:r1" "$R4:r4" "$R5:r5" "$R2:r2" "$R3:r3" "$R6:r6" "$R7:r7"; do
  pid=${entry%%:*}; name=${entry#*:}
  if wait "$pid"; then rc=0; else rc=$?; FAIL=1; fi
  echo "$rc" > "$LOG/$name.rc"
done
test "$FAIL" -eq 0 || { echo 'one or more receiver clients failed; inspect per-client stdout/stderr and exit files' >&2; exit 1; }
```

受信中の監視は別の端末で実行する。shell変数は端末間で引き継がれないため、起動端末に表示されたcandidate展開先 `$D`、device selector `$ID`、`$INSTANCE`、`$RT` と同じ実値をこの端末にも設定する。

PX4の`card-apdu HEX`は単発送信である。反復確認は既存標準照会を通常コマンドで10回実行する。

```sh
 D='/path/to/the-same-candidate-directory'; ID='usb:1-2.3'
INSTANCE='candidate-q3u4'; RT='/path/to/the-same-runtime-directory'; LOG='/path/to/the-same-private-log-directory'
for value in "$D" "$ID" "$INSTANCE" "$RT" "$LOG"; do case "$value" in ''|*'<'*|*'>'*) echo 'set copied candidate/device/instance/runtime/log values first' >&2; exit 2 ;; esac; done
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
RT="$PREFIX/tmp/px4-${RUN_ID}"; USB_DEVICE_1='/dev/bus/usb/001/004'; USB_DEVICE_2='/dev/bus/usb/001/005'
for value in "$RT" "$USB_DEVICE_1" "$USB_DEVICE_2"; do case "$value" in ''|*'<'*|*'>'*) echo 'set selected Termux paths from termux-usb -l and sysfs first' >&2; exit 2 ;; esac; done
mkdir -p "$RT"; chmod 700 "$RT"
"$D/px4-termux" --usb-device "$USB_DEVICE_1" --usb-device "$USB_DEVICE_2" --device "$ID" --firmware "$FW" --runtime-dir "$RT"
# 別セッションで §0.1 の px4d status確認コマンドをreadyになるまで再実行し、px4-ts / px4ctlを同じ $ID と $RT で実行する
```

終了と残留確認（X-STOP / X-RESID）: `wait "$P"`で自己起動したdaemonの終了を受け取った後は、そのprocessのFDとflock ownerはOSにより解放済みである。直後のendpoint/marker一覧を保存してから、専用task runtime内の残留markerだけを明示回収し、最後にruntimeが消えたことを別記録する。lock保持中のunlinkは行わない。

```sh
if kill -0 "$P" 2>/dev/null; then kill -TERM "$P"; fi
if wait "$P"; then rc=0; else rc=$?; fi
echo "$rc" > "$LOG/px4d.rc"
test "$rc" -eq 0 || { echo "px4d exit=$rc" >&2; exit "$rc"; }
ENDPOINT="$RT/px4-userland/v1/$INSTANCE"
test ! -S "$ENDPOINT/control.sock" || { echo 'control socket remains' >&2; exit 1; }
test ! -S "$ENDPOINT/stream.sock" || { echo 'stream socket remains' >&2; exit 1; }
find "$RT" -mindepth 1 -print > "$LOG/runtime-after-daemon-stop.txt" || exit $?
if [ -L "$ENDPOINT/.listener.lock" ]; then echo 'listener lock marker is a symlink; refusing cleanup' >&2; exit 1; fi
if [ -e "$ENDPOINT/.listener.lock" ]; then rm -f -- "$ENDPOINT/.listener.lock" || exit $?; fi
rmdir "$ENDPOINT" || exit $?
rmdir "$RT/px4-userland/v1" "$RT/px4-userland" "$RT" || exit $?
test ! -e "$RT" || { echo 'owned runtime namespace remains after explicit cleanup' >&2; exit 1; }
echo 'owned runtime namespace removed after daemon/lock ownership release' > "$LOG/runtime-cleanup.txt"
```

Q3U4 preflight: 両 USB device、15V adapter、両 RF lead を接続し、`px4d --list-json`の`enclosures[].devices[]`に両USB deviceと合計8 receiverが列挙されることを確認する。daemon起動後は`px4ctl status`のreadyと`px4ctl list`の8 receiverを確認してから開始する。

各配布archiveの短時間確認におけるカード物理抜去・再挿入（X-CARD-HP）:

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

各配布archiveの短時間確認におけるUSB物理切断・再接続（X-USB-HP）:

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

Termuxのarchitecture、Termux launcherのFD path、glibc/musl、native PC/SC adapterは別々のruntime/access pathとして扱う。Windows 11 x64（E17）のcandidate適用頻度は該当版のmatrixに従い、WindowsとWinSCardが対象に含まれる版ではnative libusb tuner、CARD_*、WinSCard、実機の物理回帰を以下の手順で行う。CIのbuild/offline test成功は`build-tested`のみを示し、実機matrixが未完了の間は当該行を`未認定`（`hardware-unverified`）として扱う。追加移植性確認は[移植性確認手順](platforms/portability-checks.md)に分離し、Android ad-hoc APKはdtv-android所管である。変更も新規claimもない追加runtime/access pathは毎回試験しない。ただし、毎release必須matrixに含まれる全配布archiveは実施する。

影響分類が複数にまたがる、依存範囲が不明、または非影響を証明できない場合は `unknown/ambiguous` として、影響し得るpath集合をユーザーへの説明に含める。毎release必須matrixの各配布archiveは影響判定に関係なくcandidateそのものを使い、適用するarchive/OS頻度は該当版の `docs/release-validation/<version>.md` に従う。

## 2. source / CI / 候補アーカイブ判定（毎回）

1. 候補 commit に対応する `portable userland foundation` workflow を確認する。path filter 等により自動実行されていなければ、GitHub Actions の `workflow_dispatch` でその候補 ref を指定して実行する。
2. workflow 内の unit/offline test、各 target build、source/relink、`release-candidate` と、それに依存する4つの Ubuntu/Alpine × x86_64/aarch64 artifact smoke job、Windows cross-build/PE audit/packaging job、および同じWindows ZIPを実行するWindows native offline test job がすべて成功していることを確認する。失敗 job を無視して先へ進まない。`release-candidate` はWindows cross-buildとnative testの両方に依存し、native testが消費したものと同一のWindows ZIPを取り込む。test executable artifactは取り込まない。
3. `release-candidate` artifact が候補 commit の `source_ref` を示し、9 binary archive（8 tar archiveとWindows ZIP 1つ）と対応 source archiveの計10 archive、およびその外側 `SHA256SUMS`（10件）を含むことを確認する。チェックサムを照合し、CIの manifest・license・corresponding-source・binary/source archive audit（Windows ZIPのPE/import/archive監査を含む）が通っていることを確認する。
4. 工程2の試験完了前に候補source commitが変わった場合は、そのcommitのCIとartifactを取り直す。前のcommitのarchive、手元で別途作ったbuild、PR runの古いartifactを新candidateの代用にしない。工程3の試験結果commitは候補source commitの変更ではなく、工程4ではその結果commitにタグを付けてrelease CIを実行する。
5. tag CIが生成した9 binary archiveを、実機試験に使ったfinal candidate archiveと比較する。inventory/type/mode/linkと全非文書payloadが一致することを要求する。binaryは更新された`README.md`、それを反映した`manifest.json`の`source_ref`およびREADMEのhash/size、内側`SHA256SUMS`だけを許す。source archiveは結果commitの明示的に許可された文書、commit/tree identity、それらのmanifest項目とchecksumだけを許す。source側のノート・記録も同じinventoryを維持する。公開workflowの共通検査が候補2runとtagを照合する。範囲と停止条件は [`releasing.md`](releasing.md)、記録は [`release-record.md`](release-record.md) に従う。

手動dispatchとartifact取得には次の短いCLI手順を使える。既に成功済みの自動runが候補commitと一致するなら再dispatchしない。未設定値・山括弧の未置換値・既存の展開先は停止条件とし、取得済みartifactを上書きしない。

```sh
: "${CANDIDATE_REF:?export the exact candidate source ref}"
case "$CANDIDATE_REF" in ''|*'<'*|*'>'*) echo 'invalid or unreplaced candidate ref' >&2; exit 2 ;; esac
gh workflow run build_userland.yml --ref "$CANDIDATE_REF"
gh run list --workflow build_userland.yml --limit 10
: "${RUN_ID:?export the exact successful candidate run ID}"
: "${ARTIFACT_DIR:?export a new empty artifact destination directory}"
case "$RUN_ID" in ''|*[!0-9]*) echo 'run ID must be decimal' >&2; exit 2 ;; esac
case "$ARTIFACT_DIR" in ''|*'<'*|*'>'*) echo 'invalid or unreplaced artifact directory' >&2; exit 2 ;; esac
test ! -e "$ARTIFACT_DIR" || { echo 'artifact destination already exists' >&2; exit 1; }
mkdir -m 700 "$ARTIFACT_DIR"
gh run view "$RUN_ID" --json headSha,conclusion
gh run download "$RUN_ID" --name release-candidate --dir "$ARTIFACT_DIR"
(cd "$ARTIFACT_DIR" && sha256sum -c SHA256SUMS)
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
set -eu
: "${RUN_ID:?export the exact candidate workflow run ID}"
: "${JOB_ID:?export the exact job ID from that run}"
: "${LOG:?use the private log directory from candidate preparation}"
case "$RUN_ID:$JOB_ID" in *[!0-9:]*|:*|*:) echo 'run and job IDs must be decimal' >&2; exit 2 ;; esac
test -d "$LOG"
gh run view "$RUN_ID" --json jobs --jq '.jobs[] | [.databaseId, .name, .conclusion] | @tsv'
gh run view "$RUN_ID" --job "$JOB_ID" --log > "$LOG/run-$RUN_ID-job-$JOB_ID.log"
```

正規化（UUID・署名・timestampのマスク等）による合格は認めない。両runのrunner image version/ID、toolchain識別子、libusb source checksumをrelease recordに残す。

比較のために常設の二重build jobや新しい検証スクリプトを追加しない。workflowの再実行はreleaseあたり1回でよい。

## 3. 配布アーカイブ別手順

適用対象の各配布archiveと頻度は該当版のmatrixに従い、各archiveは §0 の対応行と以下の操作を使ってnative環境で独立に実施する。Windows行はE17のWindows手順に従い、native Windows 11 x64で実施する。必須確認の一連の操作に総時間上限は設けない。B-CAS/USBの物理操作はユーザーが行い、各操作を依頼した時点から最大5分待つ。各物理操作を依頼する直前にHAOS/SCS側でCodexは`beep`、Claude Codeは`vibe`を実行する。各runの記録にはarchive名/SHA-256、source commit、環境ID/OS version/architecture、機種/USB path、実施時刻、コマンド、終了値、card status/generation/ATR/APDU応答、TS/counter、残留の有無を残す。

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

各配布archiveの短時間確認では、そのarchiveのcandidateに対しB-CASカードの抜去/再挿入とUSB切断/再接続をユーザーが実施する。daemonを起動したままcard hotplugを行い、不在検出と再挿入後のATR/reset/APDU復帰を確認してから、30秒の8 receiver captureでTS/counterを確認する。各物理操作の応答待ち上限は個別に5分とし、要求直前にHAOS/SCS側でCodexは`beep`、Claude Codeは`vibe`を実行する。新しい`tuner-hardware-verified` / `card-core-hardware-verified` claimでは、SPEC 10.3が要求する各抜差しをそのclaimのqualificationで追加実施する。該当matrixで指定された抜差しができなければ、そのartifact/claimは未完了または未認定と記録する。

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
- 必須の代表機2時間soakとOS別30分/10分確認の実施結果を各々記録する。これらに追加するsoakのユーザー決定（実施有無・時間・OS）と実施結果は別項目にする。receiver 7 fresh comparison、USB/card抜差しが追加で必要な場合は実施状態と理由
- soakのFD数・RSS記録系列と、`安定` / `頭打ち` / `判定保留` のdispositionおよび理由
- 残存blocker、既知の非blocking制限、README/support表示とrelease noteへの反映

失敗した試行は消さずに記録し、原因を切り分けた後の再試験を別試行として残す。受信設備不備や誤ったコマンド等を特定できても、失敗をpassに書き換えない。

次をすべて満たすまでStable公開へ進まない。

- Stable共通のCI、candidate artifact audit、source/license、checksum gateが成功。
- final candidateについて、毎release必須の代表機2時間soakと全配布archiveの短時間matrixが成功している。初回/majorの追加時間条件は共通頻度表に従い、これらとは別の追加soakはユーザーが指定した場合に完了する。soakのFD数・RSSが最終観測まで安定化しない持続的な増加を示し、外部要因を特定できない場合は `判定保留` とし、公開しない。
- READMEの各support claimが実証または適格なbaseline継承に対応し、未試験のmodel × runtime/access path × featureを認定表示していない。
- crash、hang、use-after-free、stale lease、再接続不能、カード経路の重大な未解決issueがない。
- 当該releaseの全配布archiveに適用される短時間matrixに未完了のartifactがある場合はStable gate未完了とし、そのままsupport表示だけで完了扱いしない。
- SPEC 10.5-2の再現性確認（同一source commitの独立2 runによる10 archive（Windows ZIPを含む）と外側`SHA256SUMS`のbyte一致）を、未実施のままpass扱いしない。toolchain/build inputが異なる比較はinconclusiveとして成功とせず、matching pairを取り直す。正規化比較による合格は認めない。
- README、LICENSE、THIRD_PARTY_NOTICES、provenance、checksum、support表示、release archiveの内容が一致し、公開前レビュー済み。
- 現行 inventory に無い機種は実機未検証としてREADMEに明示すればリリース可能。Betaを機種追加の代わりに使わない。

リリースノートには利用者向け変更、短い検証結果、必要な既知制限だけを書く。試験matrix、内部証拠系譜、詳細log、hash一覧は掲載せず、本記録とREADMEへ分離する。

<a id="px4-os-t07-recipes"></a>
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

#### Linux native PC/SC（E02/E03/E15）

T10は候補のLinux IFD `.so`、隔離pcsc-lite daemon、標準PC/SC consumerを使う。glibc/muslごとに同じ手順をそのnative runtime上で行う。PCSC daemon/clientの既存global serviceへ接続しない。compiler、make/ninja、curl、Python venv/pip、pkg-configが無い場合は管理者承認後に準備するか、native API gateを未実施として止め、system daemonで代用しない。

```sh
set -eu
WORK=$(mktemp -d "${TMPDIR:-$HOME}/px4-linux-pcsc-<run-id>.XXXXXX"); chmod 700 "$WORK"
RUN_ID='<candidate-run-id>'; SERIAL='<14-digit-base-serial-from-px4d-list-json>'; FW='<verified-firmware-file>'
IPC="$WORK/ipc"; PREFIX="$WORK/prefix"; CONF="$WORK/reader.conf.d"; LOG="$WORK/log"; RT="$WORK/runtime"
mkdir -m 700 "$IPC" "$PREFIX" "$CONF" "$LOG" "$RT" "$WORK/drivers"
curl -fL https://deb.debian.org/debian/pool/main/p/pcsc-lite/pcsc-lite_2.3.3.orig.tar.xz -o "$WORK/pcsc-lite_2.3.3.orig.tar.xz"
printf '%s  %s\n' cdff7d7153a0b37aa74e26dfec89ec7dc5c5286aa21b91b903e38739d227e8e7 "$WORK/pcsc-lite_2.3.3.orig.tar.xz" | sha256sum -c -
tar -xJf "$WORK/pcsc-lite_2.3.3.orig.tar.xz" -C "$WORK"
python3 -m venv "$WORK/venv"; "$WORK/venv/bin/pip" install --disable-pip-version-check meson==1.7.0
MESON="$WORK/venv/bin/meson"
"$MESON" setup "$WORK/pcsc-build" "$WORK/pcsc-lite-2.3.3" --prefix "$PREFIX" -Dusb=false -Dlibusb=false -Dlibudev=false -Dserial=true -Dipcdir="$IPC" -Dserialconfdir="$CONF" -Dusbdropdir="$WORK/drivers" -Dpolkit=false -Dlibsystemd=false
"$MESON" compile -C "$WORK/pcsc-build"; "$MESON" install -C "$WORK/pcsc-build"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" LD_LIBRARY_PATH="$PREFIX/lib"
test -r "$D/ifd/px4-userland-ifd.so"; test -r "$D/reader.conf.d/px4-userland.conf"
sed -e "s|@PX4_RUNTIME_DIR@|$RT|g" -e "s|@PX4_BASE_SERIAL@|$SERIAL|g" -e "s|@PX4_IFD_LIBRARY@|$D/ifd/px4-userland-ifd.so|g" -e 's|@PX4_ACCESS@|user|g' "$D/reader.conf.d/px4-userland.conf" > "$CONF/px4-userland.conf"
"$D/px4d" --device "serial:$SERIAL" --instance pcsc --firmware "$FW" --runtime-dir "$RT" > "$LOG/px4d.log" 2>&1 & PX4D_PID=$!
"$PREFIX/sbin/pcscd" --foreground --debug --config "$CONF" > "$LOG/pcscd.log" 2>&1 & PCSC_PID=$!
printf '%s\n' "$PX4D_PID" > "$LOG/px4d.pid"; printf '%s\n' "$PCSC_PID" > "$LOG/pcscd.pid"
```

native consumerには、同梱`px4-userland-ifd.so`をcandidate runbookと同じ`PKG_CONFIG_PATH`/`LD_LIBRARY_PATH`で使う。次の小probeは完全一致readerを一つだけ選択し、card presentとAPDU 10回のSW `9000`だけを記録する（ATR/card ID/response bytesは出力しない）。

```sh
cat > "$WORK/pcsc-probe.c" <<'C'
#include <PCSC/winscard.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
  SCARDCONTEXT c=0; SCARDHANDLE h=0; LONG rc; char readers[4096]; DWORD n=sizeof(readers), state=0, protocol=0, atrn=64; BYTE atr[64]; int found=0, ok=0;
  if(argc!=2) return 64;
  rc=SCardEstablishContext(SCARD_SCOPE_USER,NULL,NULL,&c); if(rc!=SCARD_S_SUCCESS) return 1;
  rc=SCardListReaders(c,NULL,readers,&n); if(rc!=SCARD_S_SUCCESS) { SCardReleaseContext(c); return 2; }
  for(char *p=readers; *p; p+=strlen(p)+1) if(strcmp(p,argv[1])==0) ++found;
  if(found!=1) { SCardReleaseContext(c); return 3; }
  rc=SCardConnect(c,argv[1],SCARD_SHARE_SHARED,SCARD_PROTOCOL_T0|SCARD_PROTOCOL_T1,&h,&protocol);
  if(rc!=SCARD_S_SUCCESS) { SCardReleaseContext(c); return 4; }
  if(SCardStatus(h,NULL,NULL,&state,&protocol,atr,&atrn)!=SCARD_S_SUCCESS || !(state&SCARD_STATE_PRESENT)) ok=5;
  for(int i=0; !ok && i<10; ++i) { const BYTE apdu[]={0x90,0x30,0,0,0}; BYTE response[258]; DWORD length=sizeof(response); const SCARD_IO_REQUEST *pci=protocol==SCARD_PROTOCOL_T0?SCARD_PCI_T0:SCARD_PCI_T1; if(SCardTransmit(h,pci,apdu,sizeof(apdu),NULL,response,&length)!=SCARD_S_SUCCESS || length<2 || response[length-2]!=0x90 || response[length-1]!=0x00) ok=6; }
  if(SCardDisconnect(h,SCARD_LEAVE_CARD)!=SCARD_S_SUCCESS && !ok) ok=7;
  if(SCardReleaseContext(c)!=SCARD_S_SUCCESS && !ok) ok=7;
  if(!ok) puts("reader_match=1 card_present=1 apdu_sw9000=10");
  return ok;
}
C
cc $(pkg-config --cflags libpcsclite) "$WORK/pcsc-probe.c" $(pkg-config --libs libpcsclite) -o "$WORK/pcsc-probe"
ldd "$WORK/pcsc-probe" | grep -F "$PREFIX/lib/libpcsclite.so"
if PCSCLITE_CSOCK_NAME="$IPC/pcscd.comm" LD_LIBRARY_PATH="$PREFIX/lib" "$WORK/pcsc-probe" 'PLEX PX-Q3U4 Internal Card Reader' > "$LOG/probe.txt" 2>&1; then PROBE_RC=0; else PROBE_RC=$?; fi
printf '%s\n' "$PROBE_RC" > "$LOG/probe.rc"
```

期待値はprobe exit 0、上記success label、candidate IFD pathを含むpcscd logである。probe実行後、owned PIDだけを次で通常停止し、各終了値・process/socket残留を記録する。

```sh
kill -INT "$PCSC_PID"
if wait "$PCSC_PID"; then PCSC_RC=0; else PCSC_RC=$?; fi
printf '%s\n' "$PCSC_RC" > "$LOG/pcscd.rc"
kill -INT "$PX4D_PID"
if wait "$PX4D_PID"; then PX4D_RC=0; else PX4D_RC=$?; fi
printf '%s\n' "$PX4D_RC" > "$LOG/px4d.rc"
ps -p "$PCSC_PID,$PX4D_PID" -o pid=,ppid=,stat=,command= > "$LOG/processes-after-stop.txt" || :
find "$IPC" \( -type s -o -type p \) -print > "$LOG/ipc-remnants.txt"
test ! -s "$LOG/processes-after-stop.txt"; test ! -s "$LOG/ipc-remnants.txt"
```

test後にtemporary `WORK`を削除するのは全証拠を別の保管先へ移し、owned process/socketが消えたことを確認した後だけとする。native consumer/daemon起動、pcsc-lite build、USB/card操作はこの文書修正では実施していない。

### E04 macOS arm64（基準darwin環境）

- 前提: T00でcandidate archiveのmanifestを`shasum -a 256 -c SHA256SUMS`で照合する。`otool -L` で libusb dylib と macOS system 以外の依存がないことを確認する。`sw_vers` を記録する。native PC/SCはmacOS標準PCSC.framework経路を使い、Homebrew `pcsc-lite`隔離recipeは任意の別stack確認であり必須前提ではない。
- 手順: §0.1（`stat -f %z`、`lsof -p`、`ps -o rss=` を用いる）。`darwin-arm64` を使う。
- 選定: `darwin-arm64`を現行頻度matrixで選択した場合に使用。

#### E04 macOS native PC/SC

T10のnative consumerはApple PCSC.frameworkを使って今回candidateごとに実施する。reader列挙だけでは合格にせず、接続、ATR、card operationまで確認する。以下のHomebrew `pcsc-lite`手順は任意の隔離alternate stackであり、標準経路の前提ではない。T00で固定した`release-candidate` runから配布archiveとmanifestを取得する。

```sh
set -eu
WORK=$(mktemp -d "${TMPDIR:-/tmp}/px4-pcsc.XXXXXX"); chmod 700 "$WORK"
RUN_ID='<candidate-release-run-id>'
gh run download "$RUN_ID" --repo Khronos31/px4-userland --name release-candidate --dir "$WORK/download"
cd "$WORK/download"; shasum -a 256 -c SHA256SUMS
ARCHIVE='<darwin-arm64 archive filename from this candidate>'
mkdir "$WORK/candidate"; tar -xzf "$ARCHIVE" -C "$WORK/candidate"; D="$WORK/candidate"
brew list --versions pcsc-lite >/dev/null 2>&1 || brew install pcsc-lite
brew list --versions pcsc-tools >/dev/null 2>&1 || brew install pcsc-tools
brew list --versions pkgconf >/dev/null 2>&1 || brew install pkgconf
BREW_PREFIX=$(brew --prefix); PCSC_PREFIX=$(brew --prefix pcsc-lite); PCSC_TOOLS_PREFIX=$(brew --prefix pcsc-tools); IPC_ROOT="$BREW_PREFIX/var/run"
IPC_SOCKET="$IPC_ROOT/pcscd.comm"; IPC_PIDFILE="$IPC_ROOT/pcscd.pid"
export LIBPCSCLITE_DELEGATE="$PCSC_PREFIX/lib/libpcsclite_real.1.dylib"
test -f "$LIBPCSCLITE_DELEGATE"
export DYLD_LIBRARY_PATH="$PCSC_PREFIX/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$PCSC_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
mkdir -m 700 "$WORK/runtime" "$WORK/reader.conf.d" "$WORK/log"
RT="$WORK/runtime"; LOG="$WORK/log"; SERIAL='<14-digit-base-serial-from-px4d-list-json>'; FW='<verified firmware file path>'
if [ -e "$IPC_SOCKET" ] || [ -e "$IPC_PIDFILE" ]; then
  echo 'Homebrew PC/SC IPC is occupied; stop without touching that service.' >&2; exit 1
fi
if lsof -nU "$IPC_SOCKET" > "$LOG/ipc-preflight-owner.txt" 2>&1; then
  echo 'Homebrew PC/SC IPC has an owner; stop without service intervention.' >&2; exit 1
fi
printf '%s\n' 'socket=absent pidfile=absent lsof=unowned' > "$LOG/ipc-preflight.txt"

PX4D_PID=; PCSC_PID=; PRIMARY_RC=0; CLEANUP_RC=0; CLEANUP_DONE=0; CURRENT_STEP=preflight
owned_child() {
  owned_info=$(ps -p "$1" -o ppid=,stat= 2>/dev/null)
  owned_ppid=$(printf '%s\n' "$owned_info" | awk '{print $1}')
  owned_stat=$(printf '%s\n' "$owned_info" | awk '{print $2}')
  [ "$owned_ppid" = "$$" ] && case "$owned_stat" in *Z*) return 1 ;; esac
}
stop_owned() {
  stop_role=$1; stop_pid=$2; stop_mode=already-exited; signal_rc=not-sent; term_rc=not-sent; kill_rc=not-sent
  if owned_child "$stop_pid"; then
    date -u +%FT%TZ > "$LOG/$stop_role.stop-requested-at"
    stop_mode=SIGINT-cooperative
    if kill -INT "$stop_pid" 2>/dev/null; then signal_rc=0; else signal_rc=$?; fi
    stop_attempt=0
    while [ "$stop_attempt" -lt 30 ] && owned_child "$stop_pid"; do sleep 1; stop_attempt=$((stop_attempt + 1)); done
    if owned_child "$stop_pid"; then
      stop_mode=SIGTERM-after-SIGINT-observation-timeout; CLEANUP_RC=1
      if kill -TERM "$stop_pid" 2>/dev/null; then term_rc=0; else term_rc=$?; fi
      stop_attempt=0
      while [ "$stop_attempt" -lt 10 ] && owned_child "$stop_pid"; do sleep 1; stop_attempt=$((stop_attempt + 1)); done
      if owned_child "$stop_pid"; then
        stop_mode=SIGKILL-after-SIGTERM-observation-timeout; CLEANUP_RC=1
        if kill -KILL "$stop_pid" 2>/dev/null; then kill_rc=0; else kill_rc=$?; fi
      fi
    fi
  fi
  if wait "$stop_pid"; then wait_rc=0; else wait_rc=$?; fi
  date -u +%FT%TZ > "$LOG/$stop_role.wait-completed-at"
  printf '%s\n' "$stop_mode" > "$LOG/$stop_role.stop-mode"
  printf '%s\n' "$signal_rc" > "$LOG/$stop_role.sigint.rc"
  printf '%s\n' "$term_rc" > "$LOG/$stop_role.sigterm.rc"
  printf '%s\n' "$kill_rc" > "$LOG/$stop_role.sigkill.rc"
  printf '%s\n' "$wait_rc" > "$LOG/$stop_role.wait.rc"
  if [ "$wait_rc" -ne 0 ]; then CLEANUP_RC=1; fi
}
cleanup_owned() {
  cleanup_input_rc=$1; cleanup_input_step=$2
  if [ "$CLEANUP_DONE" -eq 1 ]; then
    if [ "$cleanup_input_rc" -ne 0 ]; then return "$cleanup_input_rc"; fi
    return "$CLEANUP_RC"
  fi
  CLEANUP_DONE=1; trap - 0; trap '' INT TERM HUP; set +e
  if [ "$PRIMARY_RC" -eq 0 ]; then PRIMARY_RC=$cleanup_input_rc; fi
  printf '%s\n' "$PRIMARY_RC" > "$LOG/primary.rc"
  if [ "$PRIMARY_RC" -ne 0 ]; then printf '%s\n' "$cleanup_input_step" > "$LOG/primary.step"; else printf '%s\n' 'none' > "$LOG/primary.step"; fi
  if [ -n "$PCSC_PID" ]; then stop_owned pcscd "$PCSC_PID"; fi
  if [ -n "$PX4D_PID" ]; then stop_owned px4d "$PX4D_PID"; fi
  if [ -e "$IPC_SOCKET" ]; then
    printf '%s\n' 'present' > "$LOG/ipc-socket.state"
    if lsof -nU "$IPC_SOCKET" > "$LOG/ipc-socket-owner.txt" 2>&1; then
      if awk 'NR > 1 {print $2}' "$LOG/ipc-socket-owner.txt" | grep -Fx "$PCSC_PID" >/dev/null; then
        printf '%s\n' 'owned-residual' > "$LOG/ipc-socket.owner-state"
      else
        printf '%s\n' 'other-or-unattributed-owner' > "$LOG/ipc-socket.owner-state"
      fi
    else
      printf '%s\n' 'no-owner-stale-or-unattributed' > "$LOG/ipc-socket.owner-state"
    fi
    CLEANUP_RC=1
  else
    printf '%s\n' 'absent' > "$LOG/ipc-socket.state"
  fi
  if [ -e "$IPC_PIDFILE" ]; then
    if IFS= read -r ipc_pidfile_value < "$IPC_PIDFILE"; then :; else ipc_pidfile_value=unreadable; fi
    printf '%s\n' "$ipc_pidfile_value" > "$LOG/ipc-pidfile.value"
    if [ "$ipc_pidfile_value" = "$PCSC_PID" ]; then
      printf '%s\n' 'owned-residual' > "$LOG/ipc-pidfile.state"
    else
      printf '%s\n' 'other-or-unattributed' > "$LOG/ipc-pidfile.state"
    fi
    CLEANUP_RC=1
  else
    printf '%s\n' 'absent' > "$LOG/ipc-pidfile.state"
  fi
  cleanup_pids=
  if [ -n "$PCSC_PID" ]; then cleanup_pids=$PCSC_PID; fi
  if [ -n "$PX4D_PID" ]; then
    if [ -n "$cleanup_pids" ]; then cleanup_pids="$cleanup_pids,$PX4D_PID"; else cleanup_pids=$PX4D_PID; fi
  fi
  if [ -n "$cleanup_pids" ]; then
    if ps -p "$cleanup_pids" -o pid=,ppid=,stat=,command= > "$LOG/processes-after-stop.txt"; then PROCESS_RC=0; else PROCESS_RC=$?; fi
  else
    : > "$LOG/processes-after-stop.txt"; PROCESS_RC=0
  fi
  printf '%s\n' "$PROCESS_RC" > "$LOG/processes-after-stop.rc"
  if [ -s "$LOG/processes-after-stop.txt" ]; then CLEANUP_RC=1; fi
  find "$WORK" \( -type s -o -type p \) -print > "$LOG/work-ipc-remnants.txt"
  printf '%s\n' "$CLEANUP_RC" > "$LOG/cleanup.rc"
  trap - INT TERM HUP; set -eu
  if [ "$PRIMARY_RC" -ne 0 ]; then return "$PRIMARY_RC"; fi
  return "$CLEANUP_RC"
}
on_exit() {
  exit_input_rc=$?
  if [ "$PRIMARY_RC" -eq 0 ]; then PRIMARY_RC=$exit_input_rc; fi
  if cleanup_owned "$PRIMARY_RC" "$CURRENT_STEP"; then exit 0; else exit_code=$?; exit "$exit_code"; fi
}
trap 'exit 129' HUP; trap 'exit 130' INT; trap 'exit 143' TERM; trap 'on_exit' 0
CURRENT_STEP=px4d-start
"$D/px4d" --list-json > "$LOG/discovery.json"
"$D/px4d" --device "serial:$SERIAL" --instance pcsc --firmware "$FW" --runtime-dir "$RT" > "$LOG/px4d.log" 2>&1 & PX4D_PID=$!
printf '%s\n' "$PX4D_PID" > "$LOG/px4d.pid"
sed -e "s|@PX4_RUNTIME_DIR@|$RT|g" -e "s|@PX4_BASE_SERIAL@|$SERIAL|g" \
  -e "s|@PX4_IFD_LIBRARY@|$D/ifd/px4-userland-ifd.bundle|g" -e 's|@PX4_ACCESS@|user|g' \
  "$D/reader.conf.d/px4-userland.conf" > "$WORK/reader.conf.d/px4-userland.conf"
CURRENT_STEP=pcscd-start
"$PCSC_PREFIX/sbin/pcscd" --foreground --debug --config "$WORK/reader.conf.d" > "$LOG/pcscd.log" 2>&1 & PCSC_PID=$!
printf '%s\n' "$PCSC_PID" > "$LOG/pcscd.pid"
```

Homebrew `pcsc-lite`の固定IPCが占有されている場合は既存process/socketに介入せず未実施で止める。Apple PCSC.framework / launchd serviceは停止・変更しない。Homebrew clientとdaemonを使い、別socket名の指定だけでdaemon IPCを隔離できるとは扱わない。

以下のshブロックも同じ対話shellで続けて実行する。途中でcommandが非0になった場合は登録済みtrapが一次終了値を保存してcleanupする。reader指定可能なnative consumerは標準APIでreader列挙、接続、status、APDU送信を行い、raw ATR、card ID、APDU response本文は出力しない。

```sh
CURRENT_STEP=pcsc-probe-source
cat > "$WORK/pcsc-probe.cpp" <<'CPP'
#include <PCSC/winscard.h>
#include <cstring>
#include <iostream>
#include <string>
int main(int argc, char **argv) {
  SCARDCONTEXT c = 0; SCARDHANDLE h = 0; int result = 1;
  if (argc != 2) return 64;
  do {
    if (SCardEstablishContext(SCARD_SCOPE_USER, nullptr, nullptr, &c) != SCARD_S_SUCCESS) break;
    DWORD n = SCARD_AUTOALLOCATE; LPSTR names = nullptr;
    if (SCardListReaders(c, nullptr, (LPSTR)&names, &n) != SCARD_S_SUCCESS) break;
    std::string reader; int matches = 0;
    for (char *p = names; *p; p += std::strlen(p) + 1) if (std::string(p).find(argv[1]) != std::string::npos) { reader = p; ++matches; }
    SCardFreeMemory(c, names);
    if (matches != 1) { result = 2; break; }
    DWORD active = 0;
    if (SCardConnect(c, reader.c_str(), SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &h, &active) != SCARD_S_SUCCESS) { result = 3; break; }
    DWORD state = 0, protocol = 0, rn = 256, an = 64; char r[256]; BYTE atr[64];
    if (SCardStatus(h, r, &rn, &state, &protocol, atr, &an) != SCARD_S_SUCCESS || state != SCARD_STATE_PRESENT || an == 0) { result = 4; break; }
    const BYTE cmd[] = {0x90,0x30,0x00,0x00,0x00}; BYTE response[258];
    const SCARD_IO_REQUEST *pci = active == SCARD_PROTOCOL_T0 ? SCARD_PCI_T0 : SCARD_PCI_T1;
    result = 0;
    for (int i = 0; i < 10; ++i) { DWORD len = sizeof(response); if (SCardTransmit(h, pci, cmd, sizeof(cmd), nullptr, response, &len) != SCARD_S_SUCCESS) { result = 5; break; } if (len < 2 || response[len-2] != 0x90 || response[len-1] != 0x00) { result = 6; break; } }
    if (result == 0) std::cout << "reader_match=1 card_present=1 apdu_sw9000=10\n";
  } while (false);
  if (h && SCardDisconnect(h, SCARD_LEAVE_CARD) != SCARD_S_SUCCESS && result == 0) result = 7;
  if (c && SCardReleaseContext(c) != SCARD_S_SUCCESS && result == 0) result = 7;
  std::cout << "probe_exit=" << result << "\n"; return result;
}
CPP
```

```sh
CURRENT_STEP=pcsc-probe-build
c++ -std=c++17 $(pkg-config --cflags libpcsclite) "$WORK/pcsc-probe.cpp" $(pkg-config --libs libpcsclite) -o "$WORK/pcsc-probe"
CURRENT_STEP=reader-discovery
for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do
  if "$PCSC_TOOLS_PREFIX/bin/pcsc_scan" -r > "$LOG/readers.txt" 2>&1 && grep -Fq "PX4 userland ($SERIAL)" "$LOG/readers.txt"; then break; fi
  sleep 1
done
grep -F "PX4 userland ($SERIAL)" "$LOG/readers.txt"
CURRENT_STEP=native-consumer
if "$WORK/pcsc-probe" "PX4 userland ($SERIAL)" > "$LOG/probe.txt" 2>&1; then PROBE_RC=0; else PROBE_RC=$?; fi
printf '%s\n' "$PROBE_RC" > "$LOG/probe.rc"
```

合格は今回candidate bundleのpathがpcscd logにあり、readerが一意、probe exit 0、card present、APDU 10回全てSW `90 00`、出力`reader_match=1 card_present=1 apdu_sw9000=10`を満たすこと。exit 1=API/context、2=reader不一致、3=connect、4=status/card、5=transmit、6=SW不一致、7=release/disconnect、64=引数不正で、0以外は未達とする。reader名・終了値・成功件数のみ保存する。

```sh
CURRENT_STEP=normal-cleanup
cleanup_owned 0 "$CURRENT_STEP"
```

consumer終了後、記録済みの自分のPIDだけを停止し、各終了値を別記録する。process/socket/runtimeが残った場合は削除せずcleanup未達とする。正常cleanup確認後もcandidate、log、probe sourceは結果保存まで保持する。

### E05 Android Termux aarch64（基準android-aarch64、2-FD）

- 前提: Android側のTermux:API companion appをインストールし、Termux内でT00の`termux-api`/`util-linux`導入と`termux-usb -l`/`setsid`確認を完了する。`android-aarch64` の `px4-termux` を使う。native PC/SC adapter は N/A（card は portable IPC 経由）。
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
- E15-M実行recipe: Fedora hostでread-only列挙から対象USB nodeを特定し、Alpine imageをpullした時に返るdigestを記録して、そのdigestを使う。候補/ログ/runtimeを専用workdirへbindし、対象device一つだけを渡す。rootful Podmanや`--privileged`を使わず、permission/cgroup errorは未達として停止する。

```sh
set -eu
WORK='<new-private-e15m-run-dir>'; D='<extracted-linux-musl-aarch64-candidate-dir>'; FW='<verified-firmware-file>'
require_value() { name=$1 value=$2; case "$value" in ''|*'<'*|*'>'*) echo "invalid or unreplaced $name" >&2; return 1 ;; esac; }
require_value WORK "$WORK"; require_value D "$D"; require_value FW "$FW"
LOG="$WORK/log"; RT="$WORK/runtime"; mkdir -m 700 -p "$LOG" "$RT"
test -d "$D"; test -r "$FW"; test ! -e "$LOG/discovery.json"
if podman pull docker.io/library/alpine:3.22 > "$LOG/alpine-pull.txt" 2>&1; then PULL_RC=0; else PULL_RC=$?; fi
printf '%s\n' "$PULL_RC" > "$LOG/alpine-pull.rc"
if [ "$PULL_RC" -ne 0 ]; then echo 'Alpine image pull failed; do not use a pre-existing local tag' >&2; exit "$PULL_RC"; fi
IMAGE_DIGEST=$(podman image inspect --format '{{.Digest}}' docker.io/library/alpine:3.22)
test -n "$IMAGE_DIGEST"; ALPINE_IMAGE="docker.io/library/alpine@$IMAGE_DIGEST"
printf '%s\n' "$ALPINE_IMAGE" > "$LOG/alpine-image.txt"
lsusb > "$LOG/usb-before.txt"
USB_NODE='<verified-target-node-such-as-/dev/bus/usb/001/025>'
test -c "$USB_NODE"; stat "$USB_NODE" > "$LOG/usb-node.txt"
ID='usb:<verified-bus-port>'; INSTANCE='e15m'; RECEIVER='<receiver-id>'; CHANNEL='<matrix-channel>'; CAPTURE_SECONDS='<matrix-duration>'
case "$CAPTURE_SECONDS" in ''|*[!0-9]*) exit 2;; esac; test "$CAPTURE_SECONDS" -gt 0
export ID INSTANCE RECEIVER CHANNEL CAPTURE_SECONDS
if podman run --rm --name "px4-e15m-<run-id>" --network=none \
  --env ID --env INSTANCE --env RECEIVER --env CHANNEL --env CAPTURE_SECONDS \
  --device "$USB_NODE:$USB_NODE:rw" \
  --mount "type=bind,src=$D,dst=/candidate,ro=true" \
  --mount "type=bind,src=$FW,dst=/input/firmware.bin,ro=true" \
  --mount "type=bind,src=$WORK,dst=/runwork,rw=true" \
  --workdir /candidate "$ALPINE_IMAGE" sh -ec '
    uname -m; test -e /lib/ld-musl-aarch64.so.1
    test -x /candidate/px4d; test -x /candidate/px4-ts; test -x /candidate/px4ctl
    mkdir -m 700 /runwork/container-log /runwork/container-runtime
    /candidate/px4d --list-json > /runwork/container-log/discovery.json
    test -n "$ID"; test -n "$INSTANCE"; test -n "$RECEIVER"; test -n "$CHANNEL"; test -n "$CAPTURE_SECONDS"
    /candidate/px4d --device "$ID" --instance "$INSTANCE" --firmware /input/firmware.bin --runtime-dir /runwork/container-runtime > /runwork/container-log/px4d.log 2>&1 & P=$!
    on_exit() {
      rc=$?
      trap - EXIT
      kill -INT "$P" 2>/dev/null || :
      remaining=35
      while kill -0 "$P" 2>/dev/null; do
        if [ "$remaining" -le 0 ]; then
          printf "%s\\n" timeout > /runwork/container-log/px4d.rc
          printf "%s\\n" "$P" > /runwork/container-log/px4d.cleanup-timeout.txt
          exit 124
        fi
        sleep 1
        remaining=$((remaining - 1))
      done
      if wait "$P"; then daemon_rc=0; else daemon_rc=$?; fi
      printf "%s\\n" "$daemon_rc" > /runwork/container-log/px4d.rc
      if [ "$rc" -ne 0 ]; then exit "$rc"; fi
      exit "$daemon_rc"
    }
    trap on_exit EXIT
    sleep 2
    /candidate/px4ctl --device "$ID" --instance "$INSTANCE" --runtime-dir /runwork/container-runtime status > /runwork/container-log/status.json
    /candidate/px4-ts --device "$ID" --instance "$INSTANCE" --receiver "$RECEIVER" --channel "$CHANNEL" --runtime-dir /runwork/container-runtime --time "$CAPTURE_SECONDS" --output /runwork/container-log/capture.ts > /runwork/container-log/client.out 2> /runwork/container-log/client.err
  '; then CONTAINER_RC=0; else CONTAINER_RC=$?; fi
printf '%s\n' "$CONTAINER_RC" > "$LOG/container.rc"
podman ps -a --filter name="px4-e15m-<run-id>" --format '{{.ID}} {{.Status}}' > "$LOG/container-after.txt"
```

container内のdiscovery JSON、status、TS、daemon/client exit、外側のhost/device identityを保存する。device node、USB port、receiver、channel、durationは実機・該当版matrixの値に置換し、`ID`/`INSTANCE`/`RECEIVER`/`CHANNEL`/`CAPTURE_SECONDS`をPodman実行前にexportする。別node・複数deviceを渡さない。Podman/image pull不能、対象node permission拒否、候補firmware未提供は未達として止め、glibc結果で代替しない。
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
- T05は上のE17 owned daemon手順内で別の60秒captureを起動し、対応CARD modelでは同じcapture中にstatus/listと
  `card-apdu 90:30:00:00:00`を10回実行する。対象modelの既定channel planからreceiver/system/frequency/slotを設定し、
  各commandのresponse/exit/start/endとcapture全体のstart/endを保存する。各control childは15秒bounded waitとし、
  nonzero/timeoutならT05をincompleteにして外側のowned client/daemon bounded cleanupへ接続する。
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
  function Start-Ts([int]$receiver, [string]$system, [long]$frequency, [string]$slot, [int]$seconds = 30, [string]$output = 'NUL') {
    $a = @('--device', $ID, '--receiver', "$receiver", '--system', $system, '--freq', "$frequency")
    if ($slot) { $a += @('--slot', $slot) }
    $a += @('--runtime-dir', $RT, '--output', $output, '--time', "$seconds")
    $client = Start-OwnedProcess (Join-Path $D 'px4-ts.exe') $a $false
    $script:OwnedClients += $client
    return $client
  }
  function Forget-OwnedClient($client) {
    $script:OwnedClients = @($script:OwnedClients | Where-Object { $_.Process.Id -ne $client.Process.Id })
  }
  function Stop-OwnedClient($client, [int]$boundMs, [string]$name) {
    $clientPid = $client.Process.Id
    if (-not $client.Process.HasExited) {
      $closeRequested = $false
      try { $closeRequested = $client.Process.CloseMainWindow() } catch { $closeRequested = $false }
      "$closeRequested" | Out-File (Join-Path $LOG "$name.stop-requested.txt")
      if (-not $client.Process.WaitForExit($boundMs)) {
        "$clientPid" | Out-File (Join-Path $LOG "$name.cleanup-timeout.txt")
        return $false
      }
    }
    $client.Process.Refresh()
    if (-not ($client.StdoutTask.Wait(5000) -and $client.StderrTask.Wait(5000))) {
      "$clientPid" | Out-File (Join-Path $LOG "$name.drain-timeout.txt")
      return $false
    }
    $client.StdoutTask.Result | Out-File (Join-Path $LOG "$name.cleanup.out")
    $client.StderrTask.Result | Out-File (Join-Path $LOG "$name.cleanup.err")
    "$($client.Process.ExitCode)" | Out-File (Join-Path $LOG "$name.cleanup.rc")
    Forget-OwnedClient $client
    return $true
  }
  function Wait-PhysicalAction([string]$message) {
    if ([Console]::IsInputRedirected) { throw 'Physical-action confirmation requires an interactive console.' }
    Write-Host "$message Enterキーで完了を記録 (最大300秒): " -NoNewline
    $deadline = [DateTime]::UtcNow.AddSeconds(300)
    while ([DateTime]::UtcNow -lt $deadline) {
      if ([Console]::KeyAvailable) {
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::Enter) { Write-Host; return }
      }
      Start-Sleep -Milliseconds 250
    }
    throw 'Physical action was not confirmed within 300 seconds; record the gate as incomplete.'
  }
  function Invoke-Card([string]$operation, [string[]]$arguments = @(), [string]$label = $operation) {
    $output = (& (Join-Path $D 'px4ctl.exe') --device $ID --runtime-dir $RT $operation @arguments 2>> "$LOG\card.err") | Out-String
    $rc = $LASTEXITCODE
    $output | Out-File (Join-Path $LOG "$label.txt")
    "$rc" | Out-File (Join-Path $LOG "$label.rc")
    return $rc
  }
  function Invoke-BoundedControl([string]$operation, [string[]]$arguments, [string]$label, $capture) {
    $capture.Refresh()
    if ($capture.HasExited) { throw "T05 capture ended before $label; record T05 incomplete." }
    $started = [DateTime]::UtcNow.ToString('o')
    $controlArgs = @('--device', $ID, '--runtime-dir', $RT, $operation) + $arguments
    $child = Start-OwnedProcess (Join-Path $D 'px4ctl.exe') $controlArgs $true
    $script:OwnedClients += $child
    $rc = Stop-OwnedProcess $child 15000 $label
    Forget-OwnedClient $child
    $ended = [DateTime]::UtcNow.ToString('o')
    $started | Out-File (Join-Path $LOG "$label.started.txt")
    $ended | Out-File (Join-Path $LOG "$label.ended.txt")
    $capture.Refresh()
    if ($capture.HasExited) { throw "T05 capture ended before $label completed; record T05 incomplete." }
    if ($rc -ne 0) { throw "$label exited $rc during T05; record T05 incomplete." }
    return $rc
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
      Forget-OwnedClient $c
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

    # Phase 2のsource fenceをwinscard-smoke.ps1へ保存してから独立childを起動する。
    $probePath = Join-Path $LOG 'winscard-smoke.ps1'
    $runbookPath = '<runbook-checkout>\docs\release-validation.md'
    $lines = Get-Content -LiteralPath $runbookPath
    $phaseLine = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match 'Phase 2実機への引継ぎ') { $phaseLine = $i; break } }
    if ($phaseLine -lt 0) { throw 'Phase 2 script section not found in runbook.' }
    $fenceStart = -1
    for ($i = $phaseLine; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^\s*~~~powershell\s*$') { $fenceStart = $i; break } }
    if ($fenceStart -lt 0) { throw 'WinSCard script fence not found.' }
    $fenceEnd = -1
    for ($i = $fenceStart + 1; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^\s*~~~\s*$') { $fenceEnd = $i; break } }
    if ($fenceEnd -lt 0) { throw 'WinSCard script fence is incomplete.' }
    $scriptLines = $lines[($fenceStart + 1)..($fenceEnd - 1)] | ForEach-Object { $_ -replace '^    ', '' }
    Set-Content -LiteralPath $probePath -Value $scriptLines -Encoding UTF8
    if (-not (Test-Path -LiteralPath $probePath -PathType Leaf)) { throw "missing WinSCard smoke script: $probePath" }
    $probe = Start-OwnedProcess $childPowerShell @(
      '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $probePath,
      '-DllPath', (Join-Path $D 'winscard\WinSCard.dll'), '-RuntimeDir', $RT, '-DeviceSerial', $ID
    ) $true
    $probeExit = Stop-OwnedProcess $probe 30000 'winscard-smoke'
    if ($probeExit -ne 0) { throw "WinSCard smoke child exit code was $probeExit" }

    $script:OwnedClients = @()
    $captures = @()
    # Q3U4: 0/1/4/5=ISDB-S、2/3/6/7=ISDB-T。8 receiverを同時起動する。他profileはSPEC 10.2.7の該当のみ。
    foreach ($r in 0..7) {
      if ($r -in 0,1,4,5) { $captures += Start-Ts $r 'isdb-s' 1318000000 '0' }
      else { $captures += Start-Ts $r 'isdb-t' 527143000 '' }
    }
    Wait-Captures $captures

    # T05 runs as a separate finite capture; choose a valid channel/receiver from this model's release plan.
    $T05_RECEIVER = '<receiver-from-model-channel-plan>'; $T05_SYSTEM = '<isdb-t-or-isdb-s>'
    $T05_FREQUENCY_HZ = '<frequency-hz-from-model-channel-plan>'; $T05_SLOT = '<slot-or-empty>'; $T05_SECONDS = 60
    $t05CaptureStarted = [DateTime]::UtcNow.ToString('o')
    $t05Capture = Start-Ts ([int]$T05_RECEIVER) $T05_SYSTEM ([long]$T05_FREQUENCY_HZ) $T05_SLOT $T05_SECONDS (Join-Path $LOG 't05.ts')
    try {
      Invoke-BoundedControl 'status' @() 't05-status' $t05Capture | Out-Null
      Invoke-BoundedControl 'list' @() 't05-list' $t05Capture | Out-Null
      1..10 | ForEach-Object {
        Invoke-BoundedControl 'card-apdu' @('90:30:00:00:00') "t05-apdu-$($_)" $t05Capture | Out-Null
      }
      Wait-Captures @($t05Capture)
      $t05Capture.Process.ExitCode | Set-Content (Join-Path $LOG 't05-capture.rc')
      if ($t05Capture.Process.ExitCode -ne 0) { throw "T05 capture exited $($t05Capture.Process.ExitCode), expected 0." }
      if (-not (Test-Path (Join-Path $LOG 't05.ts'))) { throw 'T05 capture TS is missing.' }
      Get-FileHash -Algorithm SHA256 (Join-Path $LOG 't05.ts') | Out-File (Join-Path $LOG 't05.ts.sha256')
      'complete: capture overlapped status/list and ten successful APDU commands' | Set-Content (Join-Path $LOG 't05-state.txt')
    } catch {
      'incomplete: T05 control/capture failure; owned client cleanup is handled by the outer bounded cleanup' | Set-Content (Join-Path $LOG 't05-state.txt')
      throw
    } finally {
      $t05CaptureEnded = [DateTime]::UtcNow.ToString('o')
      $t05CaptureStarted | Out-File (Join-Path $LOG 't05-capture.started.txt')
      $t05CaptureEnded | Out-File (Join-Path $LOG 't05-capture.stage-ended.txt')
    }
    (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT status 2>> "$LOG\px4ctl.err") | Out-File "$LOG\status-after.txt"

    # T07 idle USB detach, stream detach, and new-daemon recovery are separate stages.
    (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT status 2>> "$LOG\usb-idle-status-before.err") | Out-File "$LOG\usb-idle-status-before.txt"
    Wait-PhysicalAction '待機中のtuner USBを抜いてください。'
    (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT status 2>> "$LOG\usb-idle-status-after.err") | Out-File "$LOG\usb-idle-status-after.txt"
    "$LASTEXITCODE" | Out-File "$LOG\usb-idle-status-after.rc"
    $idleDaemonState = 'running'; if ($owned.Process.HasExited) { $idleDaemonState = "exited=$($owned.Process.ExitCode)" }
    $idleDaemonState | Out-File "$LOG\usb-idle-daemon-state.txt"
    Stop-OwnedProcess $owned 35000 'px4d-idle' | Out-Null
    Wait-PhysicalAction 'tuner USBを再接続してください。'
    (& "$D\px4d.exe" --list-json 2> "$LOG\usb-discovery-reconnected.err") | Out-File "$LOG\usb-discovery-reconnected.json"
    $owned = Start-OwnedProcess (Join-Path $D 'px4d.exe') @('--device', $ID, '--firmware', $FW, '--runtime-dir', $RT, '--exit-on-stdin-eof') $true
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) { $status = (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT status 2>> "$LOG\usb-ready.err") | Out-String; if ($LASTEXITCODE -eq 0 -and $status -match 'ready=yes') { $ready = $true; break }; Start-Sleep -Seconds 1 }
    if (-not $ready) { throw 'px4d did not recover after idle USB reconnect.' }
    $cutClient = Start-Ts 0 'isdb-s' 1318000000 '0' 300 (Join-Path $LOG 'usb-stream-cut.ts')
    Wait-PhysicalAction '有限受信中にtuner USBを抜いてください。'
    if (-not $cutClient.Process.WaitForExit(300000)) { "$($cutClient.Process.Id)" | Out-File (Join-Path $LOG 'usb-stream-cut.timeout.txt'); throw 'USB cut client exceeded the bounded observation period.' }
    $cutClient.Process.Refresh(); "$($cutClient.Process.ExitCode)" | Out-File (Join-Path $LOG 'usb-stream-cut.rc')
    $cutStdoutDrained = $cutClient.StdoutTask.Wait(5000); $cutStderrDrained = $cutClient.StderrTask.Wait(5000)
    if (-not ($cutStdoutDrained -and $cutStderrDrained)) { "$($cutClient.Process.Id)" | Out-File (Join-Path $LOG 'usb-stream-cut.drain-timeout.txt'); throw 'USB cut client output drain exceeded 5 seconds.' }
    $cutClient.StdoutTask.Result | Out-File (Join-Path $LOG 'usb-stream-cut.out')
    $cutClient.StderrTask.Result | Out-File (Join-Path $LOG 'usb-stream-cut.err')
    Forget-OwnedClient $cutClient
    if ($cutClient.Process.ExitCode -ne 7) { throw "USB cut requires client exit 7, got $($cutClient.Process.ExitCode)." }
    Stop-OwnedProcess $owned 35000 'px4d-stream-cut' | Out-Null
    Wait-PhysicalAction 'tuner USBを再接続してください。'
    (& "$D\px4d.exe" --list-json 2> "$LOG\usb-discovery-after-stream.err") | Out-File "$LOG\usb-discovery-after-stream.json"
    $owned = Start-OwnedProcess (Join-Path $D 'px4d.exe') @('--device', $ID, '--firmware', $FW, '--runtime-dir', $RT, '--exit-on-stdin-eof') $true
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) { $status = (& "$D\px4ctl.exe" --device $ID --runtime-dir $RT status 2>> "$LOG\usb-final-ready.err") | Out-String; if ($LASTEXITCODE -eq 0 -and $status -match 'ready=yes') { $ready = $true; break }; Start-Sleep -Seconds 1 }
    if (-not $ready) { throw 'px4d did not recover after streaming USB reconnect.' }
    $recoveryClient = Start-Ts 0 'isdb-s' 1318000000 '0' 30 (Join-Path $LOG 'usb-recovery.ts')
    Wait-Captures @($recoveryClient)

    # CARD（daemon停止前）。抜去/再挿入を別段階にし各response/exitを保存する。
    Invoke-Card 'card-status' @() 'card-before-status' | Out-Null
    Invoke-Card 'card-atr' @() 'card-before-atr' | Out-Null
    Invoke-Card 'card-reset' @() 'card-before-reset' | Out-Null
    1..10 | ForEach-Object { Invoke-Card 'card-apdu' @('90:30:00:00:00') "card-before-apdu-$($_)" | Out-Null }
    Wait-PhysicalAction '同じCARDを物理的に抜いてください。'
    Invoke-Card 'card-status' @() 'card-removed-status' | Out-Null
    $atrRc = Invoke-Card 'card-atr' @() 'card-removed-atr'; $apduRc = Invoke-Card 'card-apdu' @('90:30:00:00:00') 'card-removed-apdu'
    if ($atrRc -ne 9 -or $apduRc -ne 9) { throw 'Removed CARD must produce NO_CARD / exit 9 for ATR and APDU.' }
    Wait-PhysicalAction '同じCARDを再挿入してください。'
    Invoke-Card 'card-status' @() 'card-reinserted-status' | Out-Null
    $atrRc = Invoke-Card 'card-atr' @() 'card-reinserted-atr'; $resetRc = Invoke-Card 'card-reset' @() 'card-reinserted-reset'
    if ($atrRc -ne 0 -or $resetRc -ne 0) { throw 'Reinserted CARD must restore ATR and reset.' }
    1..10 | ForEach-Object {
      $rc = Invoke-Card 'card-apdu' @('90:30:00:00:00') "card-reinserted-apdu-$($_)"
      if ($rc -ne 0) { throw "Reinserted CARD APDU $_ failed with exit $rc." }
    }
  } catch {
    $primary = $_
  } finally {
    $clientCleanupFailed = $false
    foreach ($client in @($script:OwnedClients)) {
      if (-not (Stop-OwnedClient $client 5000 "client-$($client.Process.Id)")) { $clientCleanupFailed = $true }
    }
    try { Stop-OwnedProcess $owned 35000 'px4d' | Out-Null }
    catch { if ($null -eq $primary) { $primary = $_ } }
    foreach ($client in @($script:OwnedClients)) {
      if (-not (Stop-OwnedClient $client 5000 "client-$($client.Process.Id)-after-daemon")) { $clientCleanupFailed = $true }
    }
    # A retry may finish the drain and remove the client from OwnedClients; retain the first-failure files but judge runtime cleanup by current owners.
    $clientCleanupFailed = @($script:OwnedClients).Count -gt 0
    if ($clientCleanupFailed -and $null -eq $primary) { $primary = 'owned client process or output drain remained incomplete; retain runtime and fail cleanup' }
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
    要求・測定しない。確定頻度表にあるWindows 30分・代表機2時間・OS別10分は必須確認として実施し、短時間確認へ置き換えない。
    これらを超える追加連続負荷試験だけをSPEC 10.5.2「Soak: user decision」に従い、ユーザーが有無・時間・対象OSを決める。
- 適用条件: Q3U4の項目（8 receiver同時captureを含む）は実際に接続した機種がQ3U4の場合だけ適用し、他profileは
  SPEC 10.2.7の該当項目を使う。TS/counterの合否はSPEC 10.2の適用条件で判定し、shell終了値だけで決めない。
  新しい汎用検証scriptやframeworkは追加しない。
