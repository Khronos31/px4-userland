# px4-userland

`px4-userland` は、PLEX PX-Q3U4、PX-M1UR、PX-S1UR、PX-W3U4、PX-MLT5PE、および e-Better DTV02A-5TS-P 向けのユーザー空間ドライバおよびツール群です。カーネルモジュールを使用せず、ユーザー空間からチューナーおよび内蔵 IC カードリーダーを制御し、MPEG-TS ストリームを出力します。機種ごとの認定範囲とOS/access path別の状態は下表を参照してください。

## 対応機種・動作環境

### 検証済みの機種

#### PLEX

| 機種 | USB ID |
|---|---|
| PX-Q3U4 | `0511:084a` |
| PX-M1UR | `0511:0854` |
| PX-S1UR | `0511:0855` |
| PX-MLT5PE | `0511:024e` |

#### e-Better

| 機種 | USB ID |
|---|---|
| DTV02A-5TS-P | `0511:924e` |

※ PX-MLT5PE は同一ハードウェアの DTV02A-5TS-P で実機検証しています。

### 未検証の機種

下記の機種は実装・列挙対象ですが、機種profileのcanonical hardware認定が未完了です。試験したruntime/access pathがある場合も、下表に示す個別範囲を超えるサポート主張は行いません。

#### PLEX

| 機種 | USB ID |
|---|---|
| PX-W3U4 | `0511:083f` |
| PX-W3PE4 | `0511:023f` |
| PX-Q3PE4 | `0511:024a` |
| PX-W3PE5 | `0511:073f` |
| PX-Q3PE5 | `0511:074a` |
| PX-MLT8PE | `0511:0252`（MLT8PE3）、`0511:0253`（MLT8PE5） |

#### e-Better

| 機種 | USB ID |
|---|---|
| DTV02-1T1S-U（実験的） | `0511:004b`（Digibest ISDB2056） |
| DTV02A-1T1S-U | `0511:004b`（Digibest ISDB2056）。ロット 2309 以降は `0511:084b`（Digibest ISDB2056N） |
| DTV02A-4TS-P | `0511:0254`（Digibest ISDB6014-4TS） |
| DTV03A-1TU（実験的。ロット 2021-11 以降） | `0511:0052`（Digibest ISDBT2071） |

### LNB 15V 非対応機種

| 機種 | 衛星放送 |
|---|---|
| PX-M1UR | ISDB-SをLNB 0Vで受信可能 |
| DTV02-1T1S-U | ISDB-SをLNB 0Vで受信可能（機種profile未認定） |
| DTV02A-1T1S-U | ISDB-SをLNB 0Vで受信可能（機種profile未認定） |
| PX-S1UR | ISDB-T専用。ISDB-S非対応 |
| DTV03A-1TU | ISDB-T専用。ISDB-S非対応（機種profile未認定） |

上記5機種の`DeviceProfile.supports_lnb_15v`は`false`です。他の列挙対象機種は仕様上`true`ですが、
実機未検証の機種について給電能力を実測済みと示すものではありません。対応機種でも既定は0Vであり、
15Vの要求にはdaemonの`--allow-lnb-power`と受信時の`--lnb-voltage 15`の両方が必要です。

### 動作環境

機能軸別の対応状況は下表のとおりです（SPEC 10.3 準拠）。

v0.2.0候補（`8c40d49`）では配布binary 9種（Windows ZIPを含む）の短時間実機確認を実施しました。
結果は[Windows（E17）](docs/platforms/validation-results.md#2026-10-10-v020-candidate-e17-windows-11-x64-実機試験q3u4必須matrix-pass--soak注記付き受入)と
[他8 archive](docs/platforms/validation-results.md#2026-10-11-v020-candidate-8-binary-archive短時間matrixe02e07e15)の検証記録を参照してください。
PX-Q3U4のreceiver 7の既知不具合は、E03での`px4_drv`との交互比較（5組）の結果を含めて同記録に記載しています。

v0.1.10では配布binary 8種の短時間実機確認を実施しました。PX-Q3U4のreceiver 7の既知不具合と、
macOSで1回観測したUSB再接続後のCC異常（追加20回では未再現）を含む判定・検証範囲は、
[今回の検証記録](docs/platforms/validation-results.md#2026-10-08-v0110-release-candidate試験)を参照してください。

| OS / 環境 | 対象model/profile | build-tested | tuner-hardware-verified | card-core-hardware-verified | native-card-adapter-verified | 備考 |
|---|---|:---:|:---:|:---:|:---:|---|
| Linux x86_64 | PX-Q3U4 | 完了 | 検証済み | 検証済み | 検証済み | 完全静的CLI + glibc/musl別IFD Handler |
| Linux x86_64 | PX-M1UR / PX-S1UR | 完了 | 検証済み | 検証済み | 検証済み | AnduinOS上でprofile別30分連続受信、機種固有T/S、カード、retune、stop/reopen、USB再接続後のdaemon再起動による復旧を確認。v0.1.9では通常の単独接続経路を既存証拠から継承し、serial衝突表示と曖昧指定拒否をcandidateで確認。同一daemonの自動USB再接続と、衝突中の`--usb-path`による個別起動は未認定。[判定根拠](docs/platforms/validation-results.md)参照。 |
| Linux x86_64 | DTV02A-5TS-P実機（PX-MLT5PEと同一のMLT5 profile） | 完了 | 検証済み | 検証済み | 未検証 | [PR #5](https://github.com/Khronos31/px4-userland/pull/5)：Ubuntu 26.04 amd64で5 tunerの地上波・BS/CS TSと内蔵カードリーダー利用を確認。IFD/PCSC経路は未確認。 |
| Linux aarch64 | 全model/profile | 完了 | 未検証 | 未検証 | 未検証 | aggregate row。Q3U4のtargeted E15 hardware smokeは実施済みだが、全model/profileを検証した意味ではない。 |
| HAOS SCS Debian/glibc x86_64 | PX-M1UR / PX-S1UR | 完了 | 検証済み | 検証済み | 検証済み | 既存candidateで30分受信、PC/SC併走、機種該当のT/S、card/USB再接続後のdaemon再起動による復旧を確認。v0.1.9では通常の単独接続経路を既存証拠から継承し、serial衝突表示と曖昧指定拒否をcandidateで確認。同一daemonの自動USB再接続と、衝突中の`--usb-path`による個別起動は未認定。[判定根拠](docs/platforms/validation-results.md)参照。 |
| HAOS Supervisor Alpine/musl x86_64 | PX-M1UR / PX-S1UR | 完了 | 未認定（一部実機試験） | 未認定（一部実機試験） | 未認定（一部PC/SC smoke） | 候補版でT/S該当系統、direct APDUとPC/SC reader smokeは成功。30分profile認定、card抜去/再挿入、反復PC/SC APDUと物理USB抜差しは未実施。 |
| macOS arm64 | PX-Q3U4 | 完了 | 検証済み | 検証済み | 検証済み | Apple Silicon。v0.1.9 candidateで8 receiver受信、card hotplug/APDU、USB reconnect後のdaemon復旧、Homebrew pcsc-lite consumerを確認。[検証結果](docs/platforms/validation-results.md)参照。 |
| macOS arm64 | PX-M1UR | 完了 | 未認定 | 未認定 | 未認定 | E04で同時接続時のlist/JSONと曖昧serial拒否を確認。受信・card・native PC/SCは未認定。 |
| macOS arm64 | PX-S1UR | 完了 | 未認定 | 未認定 | 未認定 | E04で同時接続時のlist/JSONと曖昧serial拒否を確認。受信・card・native PC/SCは未認定。 |
| Android Termux（aarch64 / armv7a / x86_64） | PX-Q3U4 | 完了 | 未認定（receiver 7のfresh参照比較条件を満たす根拠なし） | 未認定（receiver 7のfresh参照比較条件を満たす根拠なし） | 該当なし（N/A） | 各ABIのv0.1.9 candidateで2-FD launcher、8 receiver受信、card hotplug/APDU、USB reconnect後の復旧を実機確認済み。receiver 7のburstを含む個別claimの判定は[検証結果](docs/platforms/validation-results.md)参照。 |
| Android Termux（aarch64 / armv7a / x86_64） | PX-M1UR / PX-S1UR | 完了 | 未認定（一部実機試験） | 未認定（一部実機試験） | 該当なし（N/A） | 各architectureでT/S該当系統と受信中APDUを確認。card抜去/再挿入とUSB切断/再接続は未実施。APKは対象外。 |
| Android ad-hoc APK | PX-Q3U4 | 対象外 | 対象外 | 対象外 | 該当なし（N/A） | dtv-android 所管。本リポジトリの配布物・release gate には含めません（過去の内部試験記録は検証結果参照） |
| Windows 11 x64（Phase 1） | PX-Q3U4 | 完了 | 検証済み | 検証済み | 該当なし（N/A） | native libusb（WinUSB）。0.2.0 candidateでE17の8 receiver受信、card hotplug/APDU、USB再接続後のdaemon再起動、2時間soak、全局受信を確認（[検証記録](docs/platforms/validation-results.md#2026-10-10-v020-candidate-e17-windows-11-x64-実機試験q3u4必須matrix-pass--soak注記付き受入)）。既知の制限: 非ASCII endpoint pathでready行が途切れる（[Issue #52](https://github.com/Khronos31/px4-userland/issues/52)）。WinSCard互換DLLとPC/SC IFD登録はPhase 2 |
| Windows 11 x64（Phase 1） | PX-M1UR / PX-S1UR | 完了 | 検証済み | 検証済み | 該当なし（N/A） | native libusb（WinUSB）。0.2.0 candidateでprofile別30分連続受信（受信中の直接APDU 300/300）、機種固有T/S（M1UR ISDB-S 0V・15V拒否、S1UR ISDB-S拒否）、stop/reopen、card抜去/再挿入、USB切断・再接続後のdaemon再起動による復旧、同時接続時のlist/JSONと曖昧serial拒否を確認。同一lease retune、同一daemonの自動USB再接続、衝突中の`--usb-path`による個別起動は未認定。既知の制限: [Issue #52](https://github.com/Khronos31/px4-userland/issues/52)。PC/SCはPhase 2 |
| Windows 11 x64（Phase 1） | その他のmodel/profile | 完了 | 未検証（hardware-unverified） | 未検証（hardware-unverified） | 該当なし（N/A） | 実機evidenceなし |

各claimは、明記したmodel/profile × runtime/access path × featureにだけ適用されます。別のmodel/profile、runtime/access path、featureへ検証結果を推論しません。表に記載のない組合せはverified claimの対象外です。

※ Linux aarch64 は全model/profileを対象にしたhardware support claimは未認定です。E15でFedora 42/glibcとAlpine musl container上のPX-Q3U4を対象に、受信/cardの物理抜差しと再接続を含むcandidate試験を実施しました。native PC/SC IFD/adapterは未試験で、結果の範囲は[OS・環境別の検証結果](docs/platforms/validation-results.md)を参照してください。
※ Android 向けには Termux 用アーカイブ（実行ファイルおよび `px4-termux`）のみを提供しており、配布用 APK は提供していません。

## 必要条件

### ファームウェア

IT930x ファームウェアは本ソフトウェアに同梱されていません。別途用意し、`px4d` 起動時に `--firmware` オプションでパスを指定してください。

- 受理条件: ファイルサイズ 2,169 バイト、SHA-256 `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`

### 実行時ライブラリ

- **Linux**: `px4d`、`px4-ts`、`px4ctl` はPT_INTERPとDT_NEEDEDを持たないmusl完全静的ELFです。PC/SCリーダーとして利用する場合は、hostの`pcscd`が読み込むlibc別（glibcまたはmusl）のIFD Handlerが必要です。
- **macOS**: libusb は実行ファイルへ静的リンク済みで、Homebrew の libusb は不要です。`px4d`、`px4-ts`、`px4ctl` は macOS 標準のライブラリとフレームワークだけに依存します。PC/SC リーダーとして利用する場合は PC/SC デーモンが必要です。
- **Android**: libusb は実行ファイルへ静的リンク済みです。Termux 環境で `px4-termux` を利用する場合は、Termux:API アプリ、`termux-api` パッケージ（`termux-usb` を提供）、および依存関係である `util-linux`（`setsid` を提供）が必要です。Python や補助デーモンは不要です。
- **Windows 11 x64（Phase 1）**: `px4d` は libusb 1.0.30（WinUSB backend）を静的リンクします。`px4-ts` と `px4ctl` は libusb をリンクしません。実行時に動的ロードするのは Windows 標準の DLL（UCRT、および libusb が LoadLibrary する WinUSB 等）だけです。`libusb-1.0.dll` は同梱しません。kernel driverやWinUSB INFは導入しません。`px4d` はforegroundで動作し、IPC endpointは `--runtime-dir`（既定は `%LOCALAPPDATA%`）配下にsame-user private権限で作成します。cooperative shutdownのため `--exit-on-stdin-eof` を提供します。WinSCard互換DLLは提供しません（Phase 2）。

### Linux の USB アクセス権限

Linux ディストリビューション別の実機検証済み構成例は [Linux環境別の検証済み構成例](docs/platforms/README.md) を参照してください。

Linux では `px4d` の実行ユーザーが対象機種（USB ID `0511:084a`、`0511:0854`、`0511:0855`、`0511:024e`、`0511:924e`）の USB デバイスノードを読み書きできる必要があります。権限がない場合、低層原因は libusb の access denied ですが、CLI 表示は `device open: USB_IO`（終了コード 7）になります。通常の `px4d` 実行に毎回 `sudo` を使う必要はありません。

udev 環境では、対象を利用する機種だけに限定したルールを root で配置します。

```udev
# /etc/udev/rules.d/70-px4-q3u4.rules
# PX-Q3U4 を利用する場合
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="0511", ATTR{idProduct}=="084a", MODE="0660", GROUP="video"
# PX-M1UR / PX-S1UR を利用する場合
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="0511", ATTR{idProduct}=="0854", MODE="0660", GROUP="video"
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="0511", ATTR{idProduct}=="0855", MODE="0660", GROUP="video"
# PX-MLT5PE / DTV02A-5TS-P を利用する場合
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="0511", ATTR{idProduct}=="024e", MODE="0660", GROUP="video"
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="0511", ATTR{idProduct}=="924e", MODE="0660", GROUP="video"
```

カーネルに対象機種向けのカーネルドライバが導入されている環境では、DTV02A-5TS-P / PX-MLT5PE のインターフェースがカーネルドライバへバインドされるため、`px4d` は `BUSY` で失敗します（暗黙に奪いません）。`px4d` を使う間は該当のカーネルドライバを無効化するか、該当インターフェースを unbind してください。

`px4d` を実行するユーザー（systemd などのサービスアカウントを含む）を `video` group に追加し、ルールを再読込した後、デバイスを物理的に挿し直します。

```sh
sudo usermod -aG video "$USER"
sudo udevadm control --reload-rules
```

サービスとして運用する場合は、`$USER` ではなく `px4d` のサービスアカウントを `video` group に追加してください。

#### Alpine Linux / BusyBox mdev

Alpine Linux（BusyBox mdev、コールドプラグスキャンヘルパー、OpenRC 設定）での実機検証済み手順は [Alpine Linux の構成例](docs/platforms/alpine-mdev.md) を参照してください。USB ノードのパーミッションや video グループの要件は上記と同様です。

#### AppArmor

自作profileによる`px4d`の拘束、USBデバイスノードの権限、ランタイム、PC/SCの適用範囲に関する実機検証結果は [AppArmorで実行する際の注意](docs/platforms/apparmor.md) を参照する。

## 導入方法

配布アーカイブを任意のディレクトリへ展開します。Linux 向けには generic な `linux-<arch>` アーカイブは存在せず、libc およびアーキテクチャ別に以下の 4 系統が提供されます。

- `px4-userland-<version>-linux-glibc-x86_64.tar.gz`
- `px4-userland-<version>-linux-musl-x86_64.tar.gz`
- `px4-userland-<version>-linux-glibc-aarch64.tar.gz`
- `px4-userland-<version>-linux-musl-aarch64.tar.gz`

```sh
sudo install -d /opt/px4-userland
# 例: Linux x86_64 (glibc) の場合
sudo tar -xzf px4-userland-<version>-linux-glibc-x86_64.tar.gz -C /opt/px4-userland
# 例: Linux x86_64 (musl / Alpine) の場合
# sudo tar -xzf px4-userland-<version>-linux-musl-x86_64.tar.gz -C /opt/px4-userland
```

### PC/SC リーダー設定（Linux / macOS）

内蔵 IC カードリーダーを PC/SC リーダーとして認識させる場合、アーカイブ内の `reader.conf.d/px4-userland.conf` のプレースホルダーを実際のパスに置き換えて PC/SC の設定ディレクトリに配置します。

- `@PX4_RUNTIME_DIR@`: `px4d` とクライアントが共有するランタイムディレクトリ（例: `/run/px4-userland`）
- `@PX4_BASE_SERIAL@`: 対象 PX-Q3U4 の 14 桁 base serial（2 つの USB シリアルに共通する 14 桁部分）、または DTV02A-5TS-P / PX-MLT5PE の 15 桁 USB シリアル（`FRIENDLYNAME` も機種に合わせて変更してください）
- `@PX4_IFD_LIBRARY@`: Linux では `ifd/px4-userland-ifd.so`、macOS では `ifd/px4-userland-ifd.bundle` の絶対パス
- `@PX4_ACCESS@`: `user`（px4d と pcscd を同じユーザーで動かす private mode）または `group`（pcscd のサービスユーザーと px4d が共有する group mode）

`px4d --instance TOKEN`で起動した場合、生成した設定の`DEVICENAME`では`device=SERIAL`を`instance=TOKEN`に置き換えます。

Linux の group mode 配置例（`<pcsc-reader-config-dir>` は利用する pcsc-lite パッケージの reader 設定 include ディレクトリに置き換えます）:

```sh
sudo install -d -o root -g pcscd -m 0750 /run/px4-userland
sed \
  -e 's|@PX4_RUNTIME_DIR@|/run/px4-userland|g' \
  -e 's|@PX4_BASE_SERIAL@|<base-serial>|g' \
  -e 's|@PX4_IFD_LIBRARY@|/opt/px4-userland/ifd/px4-userland-ifd.so|g' \
  -e 's|@PX4_ACCESS@|group|g' \
  /opt/px4-userland/reader.conf.d/px4-userland.conf \
  | sudo install -D /dev/stdin "<pcsc-reader-config-dir>/px4-userland.conf"

# pcscd.service が User=pcscd の場合、共有 group（pcscd）に合わせて
# IPC の group mode を明示します（実効 primary group または補助グループのいずれでも可）。
sudo -g pcscd /opt/px4-userland/px4d \
  --device '<base-serial>' --firmware /path/to/firmware.bin \
  --runtime-dir /run/px4-userland --group
```

この例はrootの実効uidと `pcscd` のgroupを使います（service managerで共有groupを実効groupまたは補助グループに指定しても構いません）。private mode では `@PX4_ACCESS@` を `user` に置換し、`px4d` と `pcscd` を同じユーザーで実行します。`group` を使う場合は、pcscd のサービスユーザーが `pcscd` group に属し、runtime directory もその group で共有できるように設定してください。

macOS では `@PX4_IFD_LIBRARY@` に `ifd/px4-userland-ifd.bundle` の絶対パスを指定します。reader 設定の `LIBPATH` は bundle ディレクトリを指し、bundle 内部の dylib を直接指しません。配置後は利用する PC/SC デーモン（`pcscd`）を再起動またはリロードして設定を反映します。

#### Alpine Linux での PC/SC 設定

Alpine Linux で musl 版 IFD、pcsc-lite、OpenRC、共有グループ、ランタイムディレクトリを組み合わせた実機検証手順は [Alpine Linux の構成例](docs/platforms/alpine-mdev.md) を参照してください。PC/SC リーダーの基本設定やプレースホルダーは上記と同様です。

## 最短の使用例

1. private modeでデーモン（`px4d`）を起動します。一時runtime rootは`mktemp -d`が作る0700ディレクトリを使い、px4d終了後に削除します。

```sh
runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/px4-userland.XXXXXX")
cleanup() {
  kill "$px4d_pid" 2>/dev/null || :
  wait "$px4d_pid" 2>/dev/null || :
  rmdir "$runtime_dir" 2>/dev/null || :
}
trap cleanup EXIT
trap 'exit 130' INT TERM
/opt/px4-userland/px4d \
  --device '<base-serial>' \
  --firmware /path/to/firmware.bin \
  --runtime-dir "$runtime_dir" >/dev/null 2>&1 &
px4d_pid=$!
ready=0
i=0
while test "$i" -lt 30; do
  if /opt/px4-userland/px4ctl --device '<base-serial>' \
      --runtime-dir "$runtime_dir" status >/dev/null 2>&1; then
    ready=1
    break
  fi
  kill -0 "$px4d_pid" 2>/dev/null || break
  sleep 1
  i=$((i + 1))
done
test "$ready" = 1 || { echo 'px4d did not become ready' >&2; exit 1; }
/opt/px4-userland/px4-ts \
  --device '<base-serial>' --receiver 2 --system isdb-t \
  --frequency-khz 557142 --runtime-dir "$runtime_dir" \
  --output - --duration-seconds 30 > stream.ts
```

この一連の例はpx4dをバックグラウンドで起動し、最大30秒のreadiness確認後に受信します。終了時はpx4dを停止・reapしてからruntime rootを`rmdir`します。

## CLI 仕様

`px4d`、`px4-ts`、`px4ctl` は、同一ホスト内で同じランタイムルートディレクトリ（`--runtime-dir`、省略時の既定値は `$XDG_RUNTIME_DIR`）とランタイム接続先名を用いてプロセス間通信（IPC）を行います。既定の接続先名は観測されたシリアル番号で、エンドポイントはランタイムルート下の `px4-userland/<SERIAL>/` に作成されます。シリアル衝突時は`px4d --instance TOKEN`とクライアントの`--instance TOKEN`で接続先名を明示できます。group mode endpointへ接続する場合は、3つすべてに `--group` を指定し、共有groupを実効primary groupまたは補助グループ（supplementary group）に含めます。

### 受信機（Receiver）番号の割り当て

PX-Q3U4 に搭載されている 8 つの受信機は以下の番号に割り当てられています。

| 受信機番号 | 放送方式 | 備考 |
|:---:|:---:|---|
| 0, 1 | ISDB-S | デバイス 1（衛星放送） |
| 2, 3 | ISDB-T | デバイス 1（地上波） |
| 4, 5 | ISDB-S | デバイス 2（衛星放送） |
| 6, 7 | ISDB-T | デバイス 2（地上波） |

DTV02A-5TS-P / PX-MLT5PE の 5 つの受信機は 0〜4 番で、いずれも選局ごとに ISDB-T と ISDB-S を選択できます（同じ lease のまま切り替え可能）。`px4ctl list` では `system=ISDB-T/S` と表示され、存在しない 5〜7 番は `status` に表示されません。

| 受信機番号 | 放送方式 | 備考 |
|:---:|:---:|---|
| 0〜4 | ISDB-T / ISDB-S | 単一デバイス（`usb-present-mask=0x01`） |

1 つの受信機を同時に占有できるクライアントは 1 つです。異なる受信機同士および内蔵カードリーダーは並行して利用できます。

### Android / Termux

Termux 環境では、配布アーカイブに含まれるシェルランチャー `px4-termux` を使用して `px4d` を起動します。PX-Q3U4 が公開する 2 つの USB デバイス、および単一USBデバイス機種（PX-M1UR / PX-S1UR / DTV02A-5TS-P / PX-MLT5PE）に対するアクセス権限を Termux:API 経由で取得し、`px4d` に引き渡して動作させます。Python や補助デーモンは不要です。Termux上のM1UR/S1UR実機試験は一部機能に限られ、対応範囲は動作環境表のとおりです。

#### 必要環境の導入

Android 端末側に **Termux:API** アプリをインストールした上で、Termux 内で `termux-api` パッケージをインストールします。`termux-api` パッケージから `termux-usb` が提供され、依存関係として `util-linux`（`setsid`）も導入されます。

Android 実機の検証条件と結果は、[OS・環境別の検証結果](docs/platforms/validation-results.md) を参照してください。

```sh
pkg install termux-api
```

#### USB デバイスの確認とアクセス許可

PX-Q3U4 は 1 台につき 2 つの USB デバイス（USB ID `0511:084a`）を公開します。`termux-usb -l` を実行して接続されている USB デバイスの一覧を表示し、同一の PX-Q3U4 に対応する 2 つのデバイスパスを確認します。

```sh
termux-usb -l
```

確認した 2 つのパスそれぞれに対して `termux-usb -r` を実行します。Android 画面にアクセス許可ダイアログが表示されるので、両方のデバイスに対してアクセスを許可してください。

```sh
termux-usb -r <usb-path-1>
termux-usb -r <usb-path-2>
```

#### デーモンの起動（`px4-termux`）

配布アーカイブは、パス名に空白やシェル特殊文字を含まないディレクトリへ展開してください。

また、Android 環境では UNIX ドメインソケットのパス長上限が短いため、`$HOME` 配下などの深い階層を指定するとソケット作成時にエラー（`INVALID_ARGUMENT`）が発生します。ランタイムディレクトリには `$PREFIX/tmp` 直下の短い固定パスを使用します。

起動前にディレクトリを作成してパーミッションを設定し、確認した 2 つの USB パス、ファームウェア、14 桁の base serial、およびランタイムディレクトリを指定して `px4-termux` を起動します。2 つの `--usb-device` は自動選択されないため、利用者が明示的に指定する必要があります。

```sh
runtime_dir="$PREFIX/tmp/p4"
mkdir -p "$runtime_dir"
chmod 700 "$runtime_dir"

/path/to/px4-termux \
  --usb-device <usb-path-1> \
  --usb-device <usb-path-2> \
  --device <base-serial> \
  --firmware /path/to/firmware.bin \
  --runtime-dir "$runtime_dir"
```

PX-M1UR / PX-S1UR / DTV02A-5TS-P / PX-MLT5PE では `--usb-device` を 1 回だけ指定し、`--device` には 15 桁の USB シリアルを指定します。M1UR/S1URはTermuxの3 architectureで一部の受信・カード経路を実機確認済みですが、hotplug等を含む機種profile認定は未完了です。

同じシリアルの機器を複数のFD経路で同時起動する場合は、それぞれの`px4-termux`に異なる`--instance TOKEN`を指定し、クライアントにも対応するTOKENを渡します。Termuxの`--usb-device`は現在のUSBデバイスパスであり、物理個体を抜き差し後も追跡するIDではありません。

- `px4-termux` はフォアグラウンドで動作します。
- 停止する場合は `Ctrl+C` を入力するか、親プロセスへ `SIGINT`、`SIGTERM`、または `SIGHUP` を送信してください。通常の正常終了（graceful cleanup）では、シグナルが子プロセスグループへ伝達され、子プロセスの終了とソケットの削除が行われます。
- `px4-termux` は子プロセスグループの終了を固定40秒間待ちます。猶予時間を超過して `SIGKILL` による強制終了へ移行した場合は標準エラー出力へ警告を出力し、graceful cleanup、LNB 0V、およびランタイムエンドポイントの削除を保証できません。
- 停止後は、`px4d` や `px4-termux` 関連のプロセスが存在しないことを確認してから、利用者が作成した専用ランタイムディレクトリを削除してください（`rmdir "$runtime_dir"`）。ランチャーは指定されたランタイムルートを自動で再帰削除しません。

#### クライアントツール（`px4ctl` / `px4-ts`）の実行

デーモンの起動中は、Termux の別セッションから `px4ctl` や `px4-ts` を実行できます。別セッションにはシェル変数が引き継がれないため、起動側と同じランタイムパスを代入した上で実行してください。`--device` と `--runtime-dir` には **`px4-termux` に指定したものとまったく同一の base serial およびランタイムディレクトリ** を指定します。

```sh
runtime_dir="$PREFIX/tmp/p4"

# 状態確認（別セッション）
/path/to/px4ctl --device <base-serial> --runtime-dir "$runtime_dir" status

# 地上波の受信（別セッション）
/path/to/px4-ts --device <base-serial> --receiver 2 --system isdb-t \
  --frequency-khz 557142 --runtime-dir "$runtime_dir" \
  --output - --duration-seconds 30 > stream.ts
```

### `px4d`（デバイス所有デーモン）

対象筐体の USB デバイス（PX-Q3U4 は 2 系統、DTV02A-5TS-P / PX-MLT5PE は 1 系統）、全受信機、内蔵 IC カードリーダーを一括して所有・管理します。フォアグラウンドで動作します。

```sh
px4d --device SERIAL --firmware PATH [--usb-path BUS:ADDRESS|BUS-PORT ...] [--instance TOKEN] [--runtime-dir PATH] [--group] [--allow-lnb-power]
px4d --list
px4d --list-json
```

- `--device SERIAL`: 2 USB機種の14桁base serial、または単一USB機種の15桁USB serialを指定します。同じserialの候補が複数ある場合は任意の1台を選ばず、候補のUSB位置を表示して終了します。
- `--usb-path BUS:ADDRESS|BUS-PORT`: 現在の接続位置を明示します。単一USB機種は1回、2 USB機種はdev 1/2の2回指定します。`BUS-PORT`は`1-7.4.1`のような表記です。指定時は`--instance TOKEN`も必須です。抜き差し後の物理個体を保証する識別子ではありません。
- `--instance TOKEN`: ランタイムsocketの接続先名をシリアルから分けます。位置指定時は必須、FD起動でも指定可能です。同時利用するクライアントにも同じTOKENを指定してください。

同じシリアルでシリアル名のデーモンとTOKEN名のデーモンは同時起動できず、後から起動した方が`BUSY`で失敗します。複数台を同時利用する場合は、先に起動したデーモンも停止してそれぞれ異なる`--instance TOKEN`で起動し直してください。旧クライアントの`--device SERIAL`は稼働中のシリアル名ソケットへ接続し、USBを再列挙して接続先を選び直す機能ではありません。
- `--firmware PATH`: IT930x ファームウェアバイナリのパスを指定します（必須）。
- `--runtime-dir PATH`: ランタイムルートディレクトリを指定します（省略時は `$XDG_RUNTIME_DIR`）。
- `--allow-lnb-power`: LNB 15V 対応機種で、衛星放送受信時の給電を許可します（安全のための明示的 opt-in）。非対応の5機種は上の一覧を参照してください。
- `--fd FD [--fd FD]`: Android 環境などで、ホスト側が開いた USB ファイルディスクリプタを直接渡して起動します。PX-Q3U4 は 2 つ、PX-M1UR / PX-S1UR / DTV02A-5TS-P / PX-MLT5PE は 1 つ指定します（この場合 `--device` は任意）。
- `--list`: 接続中の対象筐体を列挙して終了します（単独で指定）。シリアル・機種・状態・USB位置・各受信機の放送方式とLNB 15V対応能力を表示します。`serial_unique=false`ならシリアル単独での選択はできません。デバイスを所有せず、ファームウェアも稼働中の `px4d` も不要です。筐体にまとめられなかった対象機種の USB デバイスは `rejected` 行で理由付きで出ます（USB ノードを開く権限が無いと `status=open_failed`）。仕様は `SPEC.md` 4.6 節です。
- `--list-json`: 同じ情報を整形しない単一JSON文書で出します。`--list`とは排他です。場所が不明な項目は`null`になり、`ungrouped_usb_devices`には`invalid_serial`または`open_failed`のUSB観測を載せます。

USB位置は書式を示す例です。

```text
$ px4d --list
serial=00001205000960 model=PX-Q3U4 usb=0511:084a status=ready receivers=8 serial_unique=true dev1_bus=1 dev1_address=28 dev1_port=1-7.4.1 dev2_bus=1 dev2_address=29 dev2_port=1-7.4.2
receiver=0 device=1 local=0 system=ISDB-S lnb_15v_supported=true
receiver=1 device=1 local=1 system=ISDB-S lnb_15v_supported=true
receiver=2 device=1 local=2 system=ISDB-T lnb_15v_supported=false
...
serial=000020263901491 model=DTV02A-5TS-P usb=0511:924e status=ready receivers=5 serial_unique=true dev1_bus=1 dev1_address=30 dev1_port=1-7.5
receiver=0 device=1 local=0 system=ISDB-T/S lnb_15v_supported=true
...
```

同一Linuxホスト内のlocalhost usbipを利用する場合は、VHCI側の2ノードを事前にopenし、`--fd FD --fd FD`で指定する。同一libusbコンテキスト内にexport元の物理機能とimport先のVHCI機能が同一シリアルで現れ、通常列挙では重複スロット（`INVALID_ARGUMENT`）となるためである。なお、LAN経由のusbip構成は未検証である。

### `px4-ts`（MPEG-TS 受信ツール）

`px4d` に接続し、指定した受信機から MPEG-TS ストリームを受信して標準出力またはファイルへ出力します。

```sh
px4-ts (--device SERIAL | --instance TOKEN) --receiver 0..7 --system isdb-t|isdb-s --frequency-khz N [--runtime-dir PATH] [--group] [OPTIONS]
```

`--channel CH` を使うと、チャンネル表記から方式と周波数、ISDB-S の slot を決められます。

```sh
px4-ts (--device SERIAL | --instance TOKEN) --receiver 0..7 --channel CH [--runtime-dir PATH] [--group] [OPTIONS]
```

- `--device SERIAL` / `--instance TOKEN`: 対象デーモンの接続先をどちらか一方で指定します。位置指定で起動したデーモンには同じTOKENを指定してください。
- `--receiver 0..7`: 利用する受信機番号（必須）。DTV02A-5TS-P / PX-MLT5PE は 0..4 です。
- `--system isdb-t|isdb-s`: 放送方式（必須）。
- `--frequency-khz N`: 受信周波数（kHz 単位、必須）。
- `--channel CH`: チャンネル表記（後述）から方式と周波数を決めます。`--system` / `--frequency-khz` とは同時指定できません。
- ISDB-T 固有設定:
  - 帯域幅は 6MHz（6000000Hz）固定です。
- ISDB-S 固有設定:
  - `--stream-id N`（TSID）または `--slot 0..11` のいずれか一方が必須です。
- 停止条件（任意指定・相互排他。両方省略した場合は明示的停止またはエラーまで連続出力）:
  - `--duration-seconds N`: 指定秒数の受信後に終了します。
  - `--packet-count N`: 指定 TS パケット数の受信後に終了します。
- その他のオプション:
  - `--output PATH`: 出力先ファイルパスを指定します（`-` で標準出力、既定値: `-`）。
  - `--tune-timeout-ms N`: チューニング待機時間（ミリ秒、範囲: 100〜30000、既定値: 10000）。
  - `--lnb-voltage 0|15`: LNB 出力電圧の要求（既定値: 0）。15V 対応機種では `px4d --allow-lnb-power` との併用が必要です。PX-M1UR と DTV02-1T1S-U / DTV02A-1T1S-U は 0V での ISDB-S 受信のみが仕様上の対象で、後者2機種は実機未検証です。
  - `--runtime-dir PATH`: `px4d` と共有するランタイムルートディレクトリ（省略時は `$XDG_RUNTIME_DIR`）。

#### `--channel` の表記

`--channel CH` は mirakc が渡すチャンネル表記を受け付け、方式と周波数、ISDB-S の slot へ変換します。受け付ける表記は利用側と共有する**互換性を保つ公開インターフェース**であり、これ以外の表記は usage error（exit 2）で拒否します。

| 表記 | 例 | 意味 | system | frequency_khz | ISDB-S stream 選択 |
|---|---|---|---|---|---|
| `T<NN>` | `T27` | 地上波物理チャンネル 13〜62 | isdb-t | `395142 + NN * 6000` | なし |
| `<NN>` | `27` | `T<NN>` と同じ（数値だけの地上波物理チャンネル） | isdb-t | `395142 + NN * 6000` | なし |
| `BS<NN>_<S>` | `BS01_0` | BS トランスポンダ NN（奇数 01〜23）、slot S（0〜11） | isdb-s | `1049480 + ((NN - 1) / 2) * 38360` | slot = S |
| `BS<NN>` | `BS01` | BS トランスポンダ NN（奇数 01〜23） | isdb-s | `1049480 + ((NN - 1) / 2) * 38360` | `--slot` か `--stream-id` が必須 |
| `CS<N>` | `CS2` | CS トランスポンダ N（偶数 2〜24） | isdb-s | `1613000 + ((N - 2) / 2) * 40000` | 既定 slot = 0。`--slot` / `--stream-id` で上書き可 |

- `--channel` と `--system` / `--frequency-khz` は同時指定できません。
- `BS<NN>_<S>` は slot を表記に含むため `--slot` / `--stream-id` と同時指定できません。
- `T<NN>` / `<NN>` は ISDB-T のため `--slot` / `--stream-id` を指定できません。
- `BS<NN>` は `--slot` または `--stream-id` が必須です。

mirakc の tuner command では、チャンネル表記をそのまま `--channel` へ渡せます。

```sh
/opt/px4-userland/px4-ts --instance <TOKEN> --receiver <N> \
  --channel {{{channel}}} --runtime-dir <RUNTIME_DIR> --output -
```

#### 終了コード

| コード | 意味 |
|:---:|---|
| `0` | 正常終了（指定秒数・パケット数到達、または正常停止） |
| `2` | 引数・構文エラー（usage） |
| `3` | デバイス未検出 / 未準備（not found / not ready） |
| `4` | 受信機が使用中（busy） |
| `5` | タイムアウト |
| `6` | IPC バージョン不一致 / プロトコルエラー |
| `7` | USB エラー / デバイス切断 |
| `8` | TS 整合性エラー（TEI・連続性エラー等） / バックプレッシャー |
| `9` | カードエラー / カードプロトコルエラー |
| `10` | ファームウェア拒否・エラー |
| `70` | 内部エラー |

### `px4ctl`（制御・診断ツール）

デバイスの状態確認や内蔵 IC カードリーダーの操作を行います。

```sh
px4ctl (--device SERIAL | --instance TOKEN) [--runtime-dir PATH] [--group] <サブコマンド>
```

#### サブコマンド一覧

- `list`: 対象デーモンインスタンスのシリアル番号、ready 状態、USB present mask、および 8 つの受信機情報（受信機番号、デバイス番号、ローカル番号、放送方式）を表示します。
- `status`: デバイス全体の稼働状態、各受信機の状態（free / leased / tuned / streaming / error）およびエラー統計を表示します。
- `card-status`: 内蔵カードリーダーのカード挿入状態、初期化状態、ATR を表示します。
- `card-atr`: カードの ATR（Answer to Reset）を取得して表示します。
- `card-reset`: カードをリセットし、ATR を表示します。
- `card-apdu HEX [--repeat N]`: コロン区切りの 16 進文字列（1〜4096 バイト、例: `00:a4:00:00`）で指定した APDU をカードへ送信し、応答を表示します。`--repeat N` で送信回数を指定可能です（指定範囲: 1〜100000）。

## 内蔵 IC カードリーダー

PX-Q3U4 内蔵の IC カードリーダーは `px4d` が管理します。本ソフトウェア自体にスクランブル復号機能は含まれません。

- **Linux / macOS**: 同梱の IFD Handler（`ifd/px4-userland-ifd.so` または `ifd/px4-userland-ifd.bundle`）を PC/SC デーモン（`pcscd` 等）へ登録することで、システム上の標準的な PC/SC リーダーとして利用できます。
- **Android**: システム PC/SC は使用せず、同一ホスト内の IPC 経由でアプリケーションからカードリーダー機能（ATR 取得、リセット、APDU 送受信）を利用します。

## 注意事項・既知の制限

> [!WARNING]
> **受信機 7（地上波）に関する制限**
> 試験個体において、地上波受信機 7（`receiver 7`）で TEI（Transport Error Indicator）や連続性エラー（continuity error）のバーストが発生することが確認されています。同一ハードウェア個体では参照カーネルドライバでも同様に再現しており、本実装固有の問題ではないと見られますが、根本原因や他ロット・他個体での発生状況は未確認です。
> このエラーが発生した場合、`px4-ts` は `PROTOCOL_ERROR` を検知して終了コード `8` で終了します。

> [!CAUTION]
> **LNB 15V 給電の安全に関する注意**
> 衛星アンテナ設備への LNB 15V 給電は、配線や他の給電機器（ブースターやテレビなど）との競合を確認した上で行ってください。LNB 給電に対応する機種でも、誤給電を防ぐため、デーモン起動時の `--allow-lnb-power` と受信時の `px4-ts --lnb-voltage 15` の双方を明示した場合に限り 15V を要求できます。
> PX-M1UR と DTV02-1T1S-U / DTV02A-1T1S-U の LNB 15V 給電はサポート対象外です。M1UR の実機開放端測定では、両オプションを指定しても 0V のままでした。DTV02系は未実測ですが、参照 `px4_drv` のLNB setterが無効です。candidate `2f555ff` はこれらT/S兼用機種の15V要求をopt-inの有無によらずGPIO書込み前に拒否します。公開済みv0.1.7でこれらの機種に15V要求を試した場合、修正版へ移行する前にUSBを物理的に抜き差しし、以前の給電状態を持ち越さないでください。給電が必要な設備では外部給電を別途用意してください。

> [!NOTE]
> **Windows: 非ASCII pathでのready行の途切れ（0.2.0）**
> runtime directory（既定は`%LOCALAPPDATA%\px4-userland`。日本語ユーザー名の場合も該当）に日本語など非ASCII文字を含むと、`px4d`のstderrに出る`px4d ready: … endpoint=…`行がその文字の手前で途切れ、改行も出ません（[Issue #52](https://github.com/Khronos31/px4-userland/issues/52)、0.2.xで修正予定）。デーモンの動作や後続のメッセージには影響しません。ready判定には`px4ctl status`を使うか、ASCIIのみの`--runtime-dir`を指定してください。

## ビルド方法

### 必要環境

- CMake 3.16 以上
- C++17 対応コンパイラ
- スレッドライブラリ（Threads）
- libusb 1.0.23 以上
- PC/SC IFD Handler をビルドする場合は pcsc-lite の開発ヘッダー（`ifdhandler.h`）

### 手順

Linuxのrelease buildでは、Alpine/musl上で`build-linux-static.sh`により3つの
production executableを作り、hostのPC/SC IFDは`build-linux-ifd.sh`で別に作る。
配布物は`linux-glibc-x86_64`、`linux-musl-x86_64`、`linux-glibc-aarch64`、
`linux-musl-aarch64`の名前を使い、generic Linux archiveは作らない。

```sh
scripts/build-linux-static.sh --output build-linux-static
scripts/build-linux-ifd.sh --libc glibc --output build-linux-ifd-glibc
scripts/build-linux-ifd.sh --libc musl --output build-linux-ifd-musl
```

macOS arm64のrelease buildでは、`build-macos-static.sh`が固定sourceのlibusb 1.0.30を
静的libraryとしてbuildし、3つのproduction executableとPC/SC IFD bundleをbuildする。

```sh
scripts/build-macos-static.sh --build-dir build-macos \
  --pcsc-include-dir "$(brew --prefix pcsc-lite)/include/PCSC"
```

固定sourceからの再buildやrelinkが必要な場合は、対応source archiveに含まれる
`BUILD-RELINK.md`の手順と`third_party/libusb-1.0.30.tar.bz2`を使用する。

Windows x64（Phase 1）のクロスビルドは、version・checksumを固定したllvm-mingw/UCRT x86_64
toolchainを`scripts/build-windows.sh`が取得して行う。libusb 1.0.30は同じtoolchainで静的ライブラリにし、
`px4d.exe`へリンクする。toolchain archiveとlibusbのbinaryはtracked fileとして同梱しない。
修正したlibusbで再リンクする場合は、対応source archiveの`BUILD-RELINK.md`にある
`--libusb-source-dir`を使う。

```sh
scripts/build-windows.sh --output build-windows
```

Windowsはlibusbを使用し、`PX4_BUILD_PCSC_IFD` と `px4-termux` は対象外とする。WinSCard互換DLLと
Microsoft PC/SC IFD登録はPhase 2以降の対象とする。

主な CMake オプション（詳細は [`CMakeLists.txt`](CMakeLists.txt) を参照）:
- `-DPX4_ENABLE_LIBUSB=ON|OFF`（既定値: ON）: libusb トランスポートのビルド
- `-DPX4_BUILD_PCSC_IFD=ON|OFF`（既定値: ON）: PC/SC IFD Handler のビルド
- `-DPX4_BUILD_TESTS=ON|OFF`（既定値: OFF）: テストのビルド

## ライセンス

本ソフトウェアは [GPL-2.0-only](LICENSE) の下で公開されています。

コードの由来、派生元、および第三者コンポーネントのライセンス通知については、[`PROVENANCE.md`](PROVENANCE.md) および [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) を参照してください。
