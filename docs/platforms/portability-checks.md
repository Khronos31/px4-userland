# PX4 追加移植性確認手順

Stable release前の必須回帰matrixは [`../release-validation.md`](../release-validation.md) の正式配布archiveに限る。本書はそのmatrix外にあるOS/ABI・access pathの追加移植性確認を保管する。以下は独立した追加確認であり、Stable release gateの頻度・合否条件に自動追加しない。対象となる配布設定・環境固有の変更時、または利用者が追加確認を依頼した場合に使う。結果は [`validation-results.md`](validation-results.md) に該当環境とsource/archiveを分けて記録し、release gate結果と混同しない。

| 環境 | 追加確認の手順 | 適用範囲・根拠 |
|---|---|---|
| Fedora x86_64 / SELinux (E08) | `packaging/fedora/README.md` の構成と再検証手順に従い、SELinux context/labelとAVC記録、実行終了値を採取する。 | Fedora packaging/access材料またはそのclaimを変更した場合の条件付き確認。 |
| Gentoo x86_64 (E09) | 現時点で記録済み手順なし。確認を依頼された場合はexact archiveをOpenRCとudev `usb` group構成で実行し、実施したコマンドと条件を記録する。 | 現在の状態は未確認。既存記録なし。 |
| NixOS x86_64 (E10) | [`nixos.md`](nixos.md) の構成例に従い、2台同時受信とcold boot後にkernel moduleが未ロードであることを確認する。 | NixOS構成手順・関連claimの追加確認。 |
| Chimera Linux x86_64 (E11) | [`chimera-linux.md`](chimera-linux.md) に従いClang native build、全test、static/dynamic binaryでの2台同時受信を行う。 | Chimera構成手順・関連claimの追加確認。 |
| Alpine x86_64 / mdev (E12) | [`alpine-mdev.md`](alpine-mdev.md) に従い`mdev -s`、`px4d --list-json`、非rootの複数機器受信と権限を確認する。 | `packaging/mdev/**` とAlpine/mdev固有材料の追加確認。 |
| OpenWrt x86_64 (E13) | 既存記録のcandidateではexit 1であり現行claimではない。procd起動停止とhotplug権限は、OpenWrt手順の新設/変更時にだけ再確認する。 | 旧記録を今回candidateの合格として継承しない。 |
| FreeBSD x86_64 (E14) | 配布target外。過去記録は履歴として保存し、現在の配布・release gateの合格に使わない。 | 現行SPECの対象外。 |
| Debian x86/i386 (E16) | [`debian-i686.md`](debian-i686.md) に従いsource archiveをnative buildし、全test、30秒受信、slow-sinkのdrop検出を行う。 | i386配布archiveはなく、source-build claimを追加する場合の確認。 |

receiver 7の比較用kernel module load/unloadは、同一機器・antenna・power・firmware・frequency・duration・capture条件を揃えた参照が必要な場合に限る追加確認である。実施場所・module操作の承認・結果はこの追加移植性確認の記録へ分離し、通常のStable gate要件にしない。

## release手順書から移した既存手順

以下は前のrelease手順書にあった追加確認の具体的な条件であり、実施頻度やStable release gateの合否要件にはしない。

### E08 Fedora x86_64（SELinux, historical-only / conditional）

- 前提: `packaging/fedora/**` または Fedora 構成手順を変更したときだけ実施する。record `29635988c5692eb9167dc082ef0b4c4e7dfb5e04` は履歴であり current claim ではない。
- 手順: [Fedora 手順](../../packaging/fedora/README.md) の手順、offline の `scripts/test-fedora.sh`、SELinux domain、一般ユーザー IPC、PC/SC、`ausearch -m AVC`。
- 選定: 当該材料の変更時のみ。配布artifactのnative確認環境には含めない。

### E09 Gentoo x86_64（historical-only / conditional）

- 記録は未commit差分に基づくため再現可能な current claim ではない。Gentoo 手順を新設・変更した場合に §0.1 を native build と OpenRC/udev `usb` グループ構成で実施する。それ以外は履歴のみ。

### E10 NixOS x86_64（historical-only / conditional）

- 前提: [NixOS 構成例](nixos.md)。record `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e` は履歴。
- 手順: §0.1 を native build で行い、宣言的 blacklist/udev を確認する。選定: 構成手順の変更時のみ。

### E11 Chimera Linux x86_64（historical-only / conditional）

- 前提: [Chimera 構成例](chimera-linux.md)。record `3e6e32a107566689a9b4bf223dca1e4e89f67cfa` は履歴。
- 手順: §0.1 を Clang/musl native build で行い、8 receiver と direct APDU を確認する。選定: 構成手順の変更時のみ。

### E12 Alpine x86_64 / mdev（historical-only / conditional）

- 前提: [Alpine/mdev 構成例](alpine-mdev.md)。`packaging/mdev/**` は配布物に含まれるため、変更時はこの環境を実施する。offline の `scripts/test-mdev.sh` も使う。record `69ae0e056abb1f3a7291c41cb8836d2c4a2bde1e` は履歴。
- 手順: `mdev -s`、非root 実行、8 receiver、direct APDU、cleanup。選定: `packaging/mdev/**` または構成手順の変更時のみ。

### E13 OpenWrt x86_64（historical-only / conditional）

- record は `29635988c5692eb9167dc082ef0b4c4e7dfb5e04` と同内容で、OpenWrt 向けソース修正はない。OpenWrt 手順を新設・変更した場合にだけ実施する。record なしのため current claim にしない。

### E14 FreeBSD x86_64（対象外）

FreeBSDは正式配布archiveのnative matrixとは別の追加移植性確認環境として扱う。validation-results.md の既存FreeBSD recordは履歴であり、今回のcandidateやStable release gateの合格に使わない。

### E16 Debian x86/i386（source-build-only）

- i386 配布 artifact は存在しない。source archive を native build し、CTest と Q3U4 の T/S 同時受信、内蔵 card APDU を確認する（[Debian i386](debian-i686.md)）。record は Stable `v0.1.3` source archive であり履歴。選定: 該当構成手順の変更時のみ。
