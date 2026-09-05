# px4-userland

`px4-userland` は PLEX PX-Q3U4（USB ID `0511:084a`）をカーネルモジュールなしで扱うユーザー空間実装です。
チューナーと内蔵ICカードリーダーを1つの `px4d` が所有し、`px4-ts` と `px4ctl` は同一ホスト内のIPCで利用します。

## 対応プラットフォーム

| プラットフォーム | 製品サポート |
|---|---|---|
| Linux | 対応対象（x86_64 archive、HAOSを含むLinux環境） |
| macOS arm64 | 対応対象 |
| Android Termux | 対応対象（aarch64 / armv7a archive） |
| Android app embedding | 統合対象。配布APKはありません |
| Windows | 非対応 |

## 配布物

`<version>` はリポジトリ直下の [`VERSION`](VERSION) と同じ値です。現在のplatform archiveは次の4つです。

| Archive | 実行環境 |
|---|---|
| `px4-userland-<version>-linux-x86_64.tar.gz` | x86_64 Linux、musl dynamic。HAOSを含むLinux環境向け |
| `px4-userland-<version>-darwin-arm64.tar.gz` | Apple Silicon macOS |
| `px4-userland-<version>-android-aarch64.tar.gz` | Android API 24以上、Bionic aarch64、Termux向け |
| `px4-userland-<version>-android-armv7a.tar.gz` | Android API 24以上、Bionic armv7a、Termux向け |

同じcandidate一式には `px4-userland-<version>-source.tar.gz` も含まれます。これは該当source snapshot、検証済み
libusb 1.0.28 source、checksums、relink手順を含むcorresponding-source archiveです。各archiveには `LICENSE`、
`README.md`、`THIRD_PARTY_NOTICES.md`、`DEPENDENCY-NOTICE.txt`、`manifest.json`、`SHA256SUMS`が含まれます。

Android archiveはlibusb 1.0.28をstatic linkし、NDKの正確なrevisionとnotice materialを含みます。Linux/macOSは
host-provided dynamic dependencyを使います。Android archiveにはIFD、APK、`px4-ts-probe`を含めません。

firmware、vendor driver、Windows用ファイルはどのarchiveにも含まれません。firmwareの取得、抽出、変換機能も
ありません。利用者がライセンスと対象機器に適合するfirmwareを別途用意し、`px4d --firmware PATH`で指定します。

Android APKは配布しません。

## 実行時の依存

### Linux

Linux archiveはmusl dynamicです。実行先に次が必要です。

- x86_64向け `/lib/ld-musl-x86_64.so.1`
- host-provided shared `libusb-1.0.so.0`
- dynamic C++ runtime。通常はAlpine由来の `libstdc++.so.6`、`libgcc_s.so.1` などのhost system libraries
  が必要で、実際の `NEEDED` 一覧はarchiveの `evidence/binary-audit.json` に記録されています
- PC/SC readerとして使う場合は、別途インストールした `pcscd` / pcsc-lite consumer

`px4d`だけが直接libusbを使用します。`px4-ts` と `px4ctl` は `px4d`へのIPC clientで、libusbを直接必要と
しません。IFD adapterもIPCだけを使い、libusbやPC/SC client libraryを直接linkしません。

### macOS

Apple Silicon macOSと、hostから利用できるdynamic libusb、system libc++などのhost system libraries、および
PC/SC consumerが必要です。実際のdynamic dependency listはarchive evidenceに記録されます。macOS archiveの
IFDは完全な `ifd/px4-userland-ifd.bundle` として収録されています。

### Android / Termux

ABIに対応するAndroid archiveを選び、TermuxまたはUSB permissionを管理するhost側でUSB file descriptorを開いて
`px4d`へ渡します。Android archiveはlibusb 1.0.28と必要なNDK runtime部分をstatic linkしているため、hostの
libusbやnative IFDは必要ありません。内蔵card readerは `px4d` のIPC経路から利用します。

## インストールとPC/SC reader設定

archiveを任意のprefixへ展開します。例えばLinuxでは次のようにします。

```sh
sudo install -d /opt/px4-userland
sudo tar -xzf px4-userland-<version>-linux-x86_64.tar.gz -C /opt/px4-userland
```

native archiveの `reader.conf.d/px4-userland.conf` はtemplateです。少なくとも次のplaceholderを、実際に配置した
pathへ置換してください。

- `@PX4_RUNTIME_DIR@`: `px4d` とclientが共有するruntime directory
- `@PX4_BASE_SERIAL@`: 対象Q3U4のbase serial
- `@PX4_IFD_LIBRARY@`: Linuxでは `ifd/px4-userland-ifd.so`、macOSではbundle directory

Linuxの例です。`<pcsc-reader-config-dir>` は利用するpcsc-lite packageのreader設定include directoryに置き換えます。

```sh
sed \
  -e 's|@PX4_RUNTIME_DIR@|/run/px4-userland|g' \
  -e 's|@PX4_BASE_SERIAL@|00001205000960|g' \
  -e 's|@PX4_IFD_LIBRARY@|/opt/px4-userland/ifd/px4-userland-ifd.so|g' \
  /opt/px4-userland/reader.conf.d/px4-userland.conf \
  | sudo install -D /dev/stdin "/path/to/pcsc-reader-config-dir/px4-userland.conf"
```

macOSでは最後の置換先を、例えば `/opt/px4-userland/ifd/px4-userland-ifd.bundle` としてください。readerの
`LIBPATH`はbundle directoryを指し、bundle内のdylibを直接指しません。配置後は利用するpcscdの設定手順に従って
readerを再読み込みします。

## CLIの利用例

`px4d`をforegroundで起動し、firmwareを別途用意したpathから読み込みます。

```sh
/opt/px4-userland/px4d \
  --device 00001205000960 \
  --firmware /path/to/firmware.bin \
  --runtime-dir /run/px4-userland
```

Androidのようにhost側が2つのUSB fdを開く経路では、同じdaemonをfd指定で起動します。

```sh
px4d --fd 3 --fd 4 --firmware "$HOME/firmware.bin" --runtime-dir "$PREFIX/var/run/px4-userland"
```

地上波を受信し、1 receiverのTSをstdoutへ出す例です。`px4d`と同じruntime directoryを指定します。

```sh
px4-ts --device 00001205000960 --receiver 2 \
  --system isdb-t --frequency-khz 557142 \
  --runtime-dir /run/px4-userland --output - --duration-seconds 30 > stream.ts
```

状態・カード操作は `px4ctl` で行います。

```sh
px4ctl --device 00001205000960 --runtime-dir /run/px4-userland status
px4ctl --device 00001205000960 --runtime-dir /run/px4-userland card-atr
px4ctl --device 00001205000960 --runtime-dir /run/px4-userland card-apdu 00A4040000
```

ISDB-Sの15V LNB要求は安全上のopt-inです。必要な場合だけdaemonに `--allow-lnb-power` を付け、アンテナ側の
負荷条件を確認してください。未指定時は15V要求を拒否します。

## 対応外

Windows runtime、Windows adapter、Windows向け配布物はこのprojectの対象外です。Windowsでは
[`tsukumijima/px4_drv`](https://github.com/tsukumijima/px4_drv)を利用してください。これは別実装であり、
`px4-userland`のCLI、IPC、reader設定との互換性はありません。

詳細なwire仕様と受入条件は [`SPEC.md`](SPEC.md)、依存関係とlicense materialは
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)、source derivationは [`PROVENANCE.md`](PROVENANCE.md)を参照してください。
