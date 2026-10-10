# AGENTS.md

## Source of truth

- Normative product requirements are in `SPEC.md`. Read the relevant section before implementation.
- If an observed device behavior conflicts with the frozen specification, update its version and rationale before changing the implementation.
- Supported device profiles and their verification status are defined by the current `SPEC.md` and `README.md`. Do not claim support for another related device without a versioned specification change and the profile-specific hardware evidence required by `SPEC.md`.

## Supported scope

- Runtime targets are Linux (including environments where kernel modules cannot be installed), Android/Termux, macOS, and Windows 11 x64 (Phase 1).
- Every target runtime must support both the tuners and its internal card reader.
- Windows Phase 1 provides `px4d`/`px4-ts`/`px4ctl` and the versioned local IPC (including `CARD_*`) for same-host consumers. It uses the same wire protocol and `control.sock`/`stream.sock` concept. WinSCard DLL compatibility and Microsoft PC/SC IFD registration are Phase 2 and out of scope now. `tsukumijima/px4_drv` remains a separate product.
- Windows hardware claims stay `hardware-unverified` until real-device evidence exists; build and offline test success is only `build-tested`.
- Android ad-hoc APK hardware testing is owned by dtv-android and is outside this repository's release gates and support claims. The APK is not a release artifact.
- FreeBSD is outside the product scope; validation-results.md entries for it are historical only.
- Firmware is not distributed, downloaded, extracted, or transformed by this repository.

## Implementation rules

- Keep the portable core C++17, exception-free and RTTI-free. Do not use glibc extensions or GNU-only APIs required for core functionality.
- Preserve glibc, musl, Bionic API 24+, macOS, and Windows x64 portability. Keep platform APIs outside the portable core; Windows-specific code lives in dedicated files under `userland/src/windows/` and behind `_WIN32`, leaving the existing POSIX code in `#else` unchanged.
- Use fixed-width integers and checked lengths at USB, firmware, IPC, ATR, APDU, and TS boundaries.
- Preserve existing tests. Do not change expectations, fixtures, mocks, or skips merely to make a failure pass; add tests for new behavior.
- Do not reintroduce Linux kernel modules, chardev/ioctl interfaces, DKMS/Debian packaging, legacy udev rules, legacy Windows-only host implementations, or implementations for devices outside the supported models. Windows Phase 1 code is libusb-based and does not add WinUSB INF or kernel components.
- Physical USB changes, card insertion/removal, antenna changes, power changes, and other hardware operations require user confirmation before execution.
- Preserve existing dirty-tree work. Use `apply_patch` for edits and do not reset, checkout, or broadly reformat unrelated files.
- Do not run `git add`, `git commit`, or `git push` unless the user explicitly requests that operation.
- Create public issues only for unresolved problems known at publication time; do not create preventive placeholder issues.

## Release notes

- GitHub Release のタイトルは `vX.Y.Z` のみとし、先頭に製品名などを付けない。
- 本文の先頭見出しは `px4-userland vX.Y.Z` とする。
- 概要は敬体（です・ます調）で簡潔に書く。
- 「主な変更」「検証」「既知の制限」は箇条書きの常体で書く。該当項目がない節は省略し、埋め草を入れない。
- 「謝辞」は敬体で書く。
- リリースノートは利用者向けの変更概要と必要な注意に絞る。内部監査記録、詳細な試験ログ、ハッシュ一覧、余計な検証 matrix は載せない。環境別の詳細が必要な場合は README 等の正本へリンクし、matrix を複製しない。

```markdown
# px4-userland vX.Y.Z

[概要を敬体で簡潔に記載]

## 主な変更
- [変更点を常体で記載]

## 検証
- [検証結果を常体で簡潔に記載]

## 既知の制限
- [必要な場合のみ、常体で記載]

## 謝辞
- [貢献者への謝意を敬体で記載]
```

## Stable release validation

- Stable 公開前の検証は [`docs/release-validation.md`](docs/release-validation.md) の順に行う。判定条件の正本は `SPEC.md` の10章であり、手順書との不一致はSPECを優先して手順書を直す。
- 配布する各主要OS/architecture binary artifactについて、final candidateの実機確認を毎回行う。短時間確認の一連の操作に総時間上限を設けない。5分はユーザーの物理操作（B-CAS/USB抜去・再挿入）の応答待ち上限であり、各操作を要求するときはHAOS側でCodexは`beep`、Claude Codeは`vibe`を実行する。5分を超える連続負荷試験はsoakとして分ける。安定性に影響し得る変更のsoak有無・時間（10分/30分/2時間）・対象OSはユーザーが決める。エージェントは選ばず、判断材料を示して決定を待つ。独自の巨大検証scriptを追加しない。
- 検証状態の語彙は `継承` / `今回再検証` / `未認定` / `対象外` に統一し、証拠は [`docs/platforms/validation-results.md`](docs/platforms/validation-results.md) へ記録する。実施・省略・非該当の根拠と失敗試行を残す。物理USB/cardの抜差しはユーザーの確認なしに行わない。
- Android ad-hoc APKの実機検証はdtv-android所管であり、本リポジトリのrelease gateに含めない。Windows Phase 1はbuild/offline testを`build-tested`として扱い、実機evidenceが得られるまで`hardware-unverified`を維持する。FreeBSDは対象外とする。

## Handoff requirements

- Report changed files, commands run, results, and unverified scope at the end of each increment.
