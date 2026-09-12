# px4-userland

`px4-userland` は、PLEX PX-Q3U4 向けのユーザー空間ドライバおよびツール群です。カーネルモジュールを使用せず、ユーザー空間からチューナーおよび内蔵 IC カードリーダーを制御し、MPEG-TS ストリームを出力します。

## 対応機種・動作環境

### 対応機種

- **PLEX PX-Q3U4**（USB ID `0511:084a`）のみ対応
  - 他の PX4 / PX5 シリーズなど関連機種での動作は未確認です。

### 動作環境

機能軸別の対応状況は下表のとおりです（SPEC 10.3 準拠）。

| OS / 環境 | build-tested | tuner-hardware-verified | card-core-hardware-verified | native-card-adapter-verified | 備考 |
|---|:---:|:---:|:---:|:---:|---|
| Linux x86_64 | 完了 | 検証済み | 検証済み | 検証済み | 完全静的CLI + glibc/musl別IFD Handler |
| Linux aarch64 | 完了 | 未検証 | 未検証 | 未検証 | build-tested / hardware-unverified（実機物理試験未実施） |
| macOS arm64 | 完了 | 検証済み | 検証済み | 検証済み | Apple Silicon（PC/SC IFD bundle含む） |
| Android Termux（aarch64 / armv7a / x86_64） | 完了 | 検証済み | 検証済み | 該当なし（N/A） | 静的CLI + `px4-termux`（native adapter非対象） |
| Android ad-hoc APK | 完了 | 検証済み | 検証済み | 該当なし（N/A） | 内部試験器具（配布物に含まれません） |
| Windows | — | — | — | — | 非対応 / 対象外（out of scope） |

※ Linux aarch64 配布物は CI の build / artifact 監査のみ完了しており、チューナー・カード・IFD の実機検証は未実施（`build-tested / hardware-unverified`）です。glibc / musl ともに実機物理試験は実施していません（過去環境での検証履歴については [OS・環境別の検証結果](docs/platforms/validation-results.md) を参照）。
※ Android 向けには Termux 用アーカイブ（実行ファイルおよび `px4-termux`）のみを提供しており、配布用 APK は提供していません。

## 必要条件

### ファームウェア

IT930x ファームウェアは本ソフトウェアに同梱されていません。別途用意し、`px4d` 起動時に `--firmware` オプションでパスを指定してください。

- 受理条件: ファイルサイズ 2,169 バイト、SHA-256 `5213a5a38872661277a2cc1b2dfdfe88faf06f41205f460f3b51857f0568b484`

### 実行時ライブラリ

- **Linux**: `px4d`、`px4-ts`、`px4ctl` はPT_INTERPとDT_NEEDEDを持たないmusl完全静的ELFです。PC/SCリーダーとして利用する場合は、hostの`pcscd`が読み込むlibc別（glibcまたはmusl）のIFD Handlerが必要です。
- **macOS**: ホスト環境の libusb、PC/SC デーモン。
- **Android**: libusb は実行ファイルへ静的リンク済みです。Termux 環境で `px4-termux` を利用する場合は、Termux:API アプリ、`termux-api` パッケージ（`termux-usb` を提供）、および依存関係である `util-linux`（`setsid` を提供）が必要です。Python や補助デーモンは不要です。

### Linux の USB アクセス権限

Linux ディストリビューション別の実機検証済み構成例は [Linux環境別の検証済み構成例](docs/platforms/README.md) を参照してください。

Linux では `px4d` の実行ユーザーが PX-Q3U4（USB ID `0511:084a`）の USB デバイスノードを読み書きできる必要があります。権限がない場合、低層原因は libusb の access denied ですが、CLI 表示は `device open: USB_IO`（終了コード 7）になります。通常の `px4d` 実行に毎回 `sudo` を使う必要はありません。

udev 環境では、対象を PX-Q3U4 だけに限定したルールを root で配置します。

```udev
# /etc/udev/rules.d/70-px4-q3u4.rules
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="0511", ATTR{idProduct}=="084a", MODE="0660", GROUP="video"
```

`px4d` を実行するユーザー（systemd などのサービスアカウントを含む）を `video` group に追加し、ルールを再読込した後、デバイスを物理的に挿し直します。

```sh
sudo usermod -aG video "$USER"
sudo udevadm control --reload-rules
```

サービスとして運用する場合は、`$USER` ではなく `px4d` のサービスアカウントを `video` group に追加してください。

#### Alpine Linux / BusyBox mdev

Alpine Linux（BusyBox mdev、コールドプラグスキャンヘルパー、OpenRC 設定）での実機検証済み手順は [Alpine Linux の構成例](docs/platforms/alpine-mdev.md) を参照してください。USB ノードのパーミッションや video グループの要件は上記と同様です。

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
- `@PX4_BASE_SERIAL@`: 対象 PX-Q3U4 の 14 桁 base serial（2 つの USB シリアルに共通する 14 桁部分）
- `@PX4_IFD_LIBRARY@`: Linux では `ifd/px4-userland-ifd.so`、macOS では `ifd/px4-userland-ifd.bundle` の絶対パス
- `@PX4_ACCESS@`: `user`（px4d と pcscd を同じユーザーで動かす private mode）または `group`（pcscd のサービスユーザーと px4d が共有する group mode）

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

`px4d`、`px4-ts`、`px4ctl` は、同一ホスト内で同じランタイムルートディレクトリ（`--runtime-dir`、省略時の既定値は `$XDG_RUNTIME_DIR`）と 14 桁の base serial（`--device`）を用いてプロセス間通信（IPC）を行います。実際のエンドポイントは、ランタイムルート下の `px4-userland/<BASE_SERIAL>/` に作成されます。group mode endpointへ接続する場合は、3つすべてに `--group` を指定し、共有groupを実効primary groupまたは補助グループ（supplementary group）に含めます。

### 受信機（Receiver）番号の割り当て

PX-Q3U4 に搭載されている 8 つの受信機は以下の番号に割り当てられています。

| 受信機番号 | 放送方式 | 備考 |
|:---:|:---:|---|
| 0, 1 | ISDB-S | デバイス 1（衛星放送） |
| 2, 3 | ISDB-T | デバイス 1（地上波） |
| 4, 5 | ISDB-S | デバイス 2（衛星放送） |
| 6, 7 | ISDB-T | デバイス 2（地上波） |

1 つの受信機を同時に占有できるクライアントは 1 つです。異なる受信機同士および内蔵カードリーダーは並行して利用できます。

### Android / Termux

Termux 環境では、配布アーカイブに含まれるシェルランチャー `px4-termux` を使用して `px4d` を起動します。PX-Q3U4 が公開する 2 つの USB デバイスに対するアクセス権限を Termux:API 経由で取得し、`px4d` に引き渡して動作させます。Python や補助デーモンは不要です。

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

PX-Q3U4 の USB デバイス（2 系統）、8 つの受信機、内蔵 IC カードリーダーを一括して所有・管理します。フォアグラウンドで動作します。

```sh
px4d --device BASE_SERIAL --firmware PATH [--runtime-dir PATH] [--group] [--allow-lnb-power]
```

- `--device BASE_SERIAL`: 対象 PX-Q3U4 の 14 桁 base serial を指定します。
- `--firmware PATH`: IT930x ファームウェアバイナリのパスを指定します（必須）。
- `--runtime-dir PATH`: ランタイムルートディレクトリを指定します（省略時は `$XDG_RUNTIME_DIR`）。
- `--allow-lnb-power`: 衛星放送受信時の LNB 15V 給電を許可します（安全のための明示的 opt-in）。
- `--fd FD --fd FD`: Android 環境などで、ホスト側が開いた 2 つの USB ファイルディスクリプタを直接渡して起動します（この場合 `--device` は任意）。

### `px4-ts`（MPEG-TS 受信ツール）

`px4d` に接続し、指定した受信機から MPEG-TS ストリームを受信して標準出力またはファイルへ出力します。

```sh
px4-ts --device BASE_SERIAL --receiver 0..7 --system isdb-t|isdb-s --frequency-khz N [--runtime-dir PATH] [--group] [OPTIONS]
```

- `--device BASE_SERIAL`: 対象デバイスの base serial（必須）。
- `--receiver 0..7`: 利用する受信機番号（必須）。
- `--system isdb-t|isdb-s`: 放送方式（必須）。
- `--frequency-khz N`: 受信周波数（kHz 単位、必須）。
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
  - `--lnb-voltage 0|15`: LNB 出力電圧（既定値: 0）。15V 給電には `px4d --allow-lnb-power` との併用が必要です。
  - `--runtime-dir PATH`: `px4d` と共有するランタイムルートディレクトリ（省略時は `$XDG_RUNTIME_DIR`）。

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
px4ctl --device BASE_SERIAL [--runtime-dir PATH] [--group] <サブコマンド>
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
> 衛星アンテナ設備への LNB 15V 給電は、配線や他の給電機器（ブースターやテレビなど）との競合を確認した上で行ってください。誤った給電による機器破損を防ぐため、デーモン起動時の `--allow-lnb-power` と受信時の `px4-ts --lnb-voltage 15` の双方が明示的に指定された場合のみ 15V 給電を有効化します。

## ビルド方法

### 必要環境

- CMake 3.20 以上
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

固定sourceからの再buildやrelinkが必要な場合は、対応source archiveに含まれる
`BUILD-RELINK.md`の手順と`third_party/libusb-1.0.30.tar.bz2`を使用する。

主な CMake オプション（詳細は [`CMakeLists.txt`](CMakeLists.txt) を参照）:
- `-DPX4_ENABLE_LIBUSB=ON|OFF`（既定値: ON）: libusb トランスポートのビルド
- `-DPX4_BUILD_PCSC_IFD=ON|OFF`（既定値: ON）: PC/SC IFD Handler のビルド
- `-DPX4_BUILD_TESTS=ON|OFF`（既定値: OFF）: テストのビルド

## ライセンス

本ソフトウェアは [GPL-2.0-only](LICENSE) の下で公開されています。

コードの由来、派生元、および第三者コンポーネントのライセンス通知については、[`PROVENANCE.md`](PROVENANCE.md) および [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) を参照してください。
