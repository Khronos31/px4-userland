# px4-userland

`px4-userland`は、PLEX PX-Q3U4の8チューナーと内蔵ICカードリーダーを、カーネルモジュールなしで扱うための
libusbベースのユーザー空間実装です。現在は開発中であり、一般向けreleaseはまだ完成していません。

対応機種はPX-Q3U4 (`0511:084a`) だけです。チップやUSB IDが近い他製品は、動作しても未確認・非対応として扱います。

## Target matrix

| Runtime | Status | Goal |
|---|---|---|
| Linux（カーネルドライバを導入できない環境を含む） | development | tuner + internal card reader |
| Android / Termux | hardware-tested, development | tuner + internal card reader |
| Android / ad-hoc APK | test path only | tuner + internal card reader |
| macOS | development | tuner + internal card reader |
| Windows | unsupported | Use [`tsukumijima/px4_drv`](https://github.com/tsukumijima/px4_drv) |

Windows向けの`tsukumijima/px4_drv`は別製品・別interfaceです。`px4-userland`のCLI、IPC、設定との互換代替ではありません。

Android APKはAndroid USB Host経路の実機検証に使うad-hoc試験器具であり、`px4-userland`の配布物には含めません。

## Firmware

IT930x firmwareはライセンスを確認できないため、ソース、archive、release artifactへ同梱しません。
runtimeでは利用者が別途用意したfirmware pathを明示します。ダウンロード、vendor driverからの抽出、変換も
このプロジェクトの対象外です。

## Development

固定仕様と受入条件は[SPEC.md](SPEC.md)を参照してください。portable coreはC++17、USB accessはlibusb-1.0、
local IPCはfilesystem Unix domain socketを使用します。Linux/glibc、Linux/musl、Android/Bionic、macOSを対象とし、
Q3U4のtunerとinternal card readerを同じ長寿命processから所有する構成です。

## License and provenance

License: [GPL-2.0-only](LICENSE). Source derivation and file-level notices are recorded in
[PROVENANCE.md](PROVENANCE.md); third-party and release-bundle obligations are tracked in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
