# px4-userland

PLEX PX-Q3U4（`0511:084a`）の8チューナーと内蔵ICカードリーダーを、カーネルモジュールなしで扱うlibusbベースの
ユーザー空間実装です。現在は開発中で、一般向けreleaseは未完成です。

対象はLinux、Android/Termux、Androidのad-hoc試験経路、macOSです。Windowsはここではサポートしていません。
Windows利用者は、Windows対応の事実上の標準である[`tsukumijima/px4_drv`](https://github.com/tsukumijima/px4_drv)を
利用してください。これは別製品・別interfaceであり、px4-userlandのCLI/IPC/configとは互換性がありません。

## Target matrix

| Runtime | Status | Goal |
|---|---|---|
| Linux（カーネルドライバを導入できない環境を含む） | development | tuner + internal card reader |
| Android / Termux | hardware-tested, development | tuner + internal card reader |
| Android / ad-hoc APK | test path only | tuner + internal card reader |
| macOS | development | tuner + internal card reader |
| Windows | unsupported | Use [`tsukumijima/px4_drv`](https://github.com/tsukumijima/px4_drv) |

Android binary releaseのsource、notice、static/dynamic dependency検証ゲートは
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)に定義しています。一般向けreleaseは、このengineering distribution
contractを満たす対応するsource archiveと検証記録が揃うまで行いません。

IT930x firmwareは同梱しません。利用者が別途用意したfirmware pathをruntimeで指定します。ダウンロード、抽出、変換は
プロジェクトの対象外です。

仕様と受入条件は[SPEC.md](SPEC.md)、GPL-2.0-onlyは[LICENSE](LICENSE)、source derivationとfile-level noticesは
[PROVENANCE.md](PROVENANCE.md)に記録しています。
