# Stable リリース手順

この文書は px4-userland の Stable リリース公開手順を定める。リリース候補の固定、事前検証、タグの付与、CI 生成アーティファクトの照合、GitHub Release の公開、公開後記録の反映までの全工程を扱う。

合否条件と認定条件の正本は [`SPEC.md`](../SPEC.md) 10章、実機検証の実行手順は [`docs/release-validation.md`](release-validation.md)、リリースノートの記述規則は [`AGENTS.md`](../AGENTS.md)「Release notes」節を参照する。本書は公開作業の具体的な実行手順に特化して記述する。

## 公開アーティファクト構成

GitHub Release に公開するアセットは、タグ push を契機として CI（release CI）が生成した以下の11ファイルである。

- 配布バイナリアーカイブ 9種:
  - `px4-userland-<version>-linux-glibc-x86_64.tar.gz`
  - `px4-userland-<version>-linux-musl-x86_64.tar.gz`
  - `px4-userland-<version>-linux-glibc-aarch64.tar.gz`
  - `px4-userland-<version>-linux-musl-aarch64.tar.gz`
  - `px4-userland-<version>-darwin-arm64.tar.gz`
  - `px4-userland-<version>-android-aarch64.tar.gz`
  - `px4-userland-<version>-android-armv7a.tar.gz`
  - `px4-userland-<version>-android-x86_64.tar.gz`
  - `px4-userland-<version>-windows-x86_64.zip`
- 対応ソースアーカイブ 1種:
  - `px4-userland-<version>-source.tar.gz`
- 外側チェックサムファイル 1種:
  - `SHA256SUMS`（上記10アーカイブの SHA-256 ハッシュを記録したもの）

公開対象はタグ CI が生成した上記アーティファクトである。実機試験に用いた候補アーティファクトそのものは公開物とせず、タグ CI の生成物と展開照合し、文書以外のペイロードが byte-identical であることを確認したうえで公開する。

## リリース工程

Stable リリースは次の13工程に沿って進める。

### 1. 候補コミットの固定と候補アーティファクトの生成

リリース候補のソースコミットを確定し、GitHub Actions のワークフロー `.github/workflows/build_userland.yml`（workflow 名: `portable userland foundation`）を実行して `release-candidate` アーティファクトを生成する。

- アーティファクトには、9種のバイナリアーカイブ、1種の対応ソースアーカイブ、外側 `SHA256SUMS` の計11ファイルが含まれる。
- このワークフローの役割はアーティファクトの生成に限定され、タグや GitHub Release の作成は後続の工程で行う（SPEC §10.4）。

### 2. リリース前検証と再現性確認

生成された候補アーティファクトを用い、[`docs/release-validation.md`](release-validation.md) に従って全対象環境での実機確認と仕様変更試験を実施する。

- 同一ソースコミットに対する独立した2回の CI candidate run を突き合わせ、SPEC §10.5-2 に定める再現性確認（全10アーカイブ本体および外側 `SHA256SUMS` の byte-identical 照合、runner image と実効 build input の一致確認）を完了する。
- 物理操作や連続負荷試験（soak）の判断基準は `docs/release-validation.md` および `AGENTS.md` の規定に従う。

### 3. 試験結果の記録

実機検証および再現性確認の結果を、文書ファイルのみのコミットとして作業ツリーに記録する。

- 対象ファイル: `README.md`、`SPEC.md`、`docs/platforms/validation-results.md`、`docs/release-validation.md`
- コミットおよび push の実行は、ユーザーの明示的な依頼を受けた場合に進める（AGENTS.md 共通規則）。

### 4. main ブランチへの統合

結果コミットを main ブランチへ取り込む。

- 取り込み経路には、Pull Request を経由したマージ（PR の CI 成功を確認したマージコミット）または main ブランチへの直接 push を用いる。
- PR を経由する場合は、関連する全 CI ジョブの成功を確認してからマージする。

### 5. コミット差分の確認

タグを付与する main 上の結果コミットと、工程2で実機試験に用いた候補コミットとの差分を確認する。

```sh
git diff --stat <candidate-commit> <result-commit>
```

差分が文書ファイル（`README.md`、`SPEC.md`、`docs/platforms/validation-results.md`、`docs/release-validation.md` 等）のみに限定されていることを確認する。

### 6. リリースタグの付与と push

main ブランチ上の結果コミットに対し、注釈付きタグ（annotated tag）を作成してリモートへ push する。

- タグ名形式: `vX.Y.Z`
- タグメッセージ: `px4-userland vX.Y.Z`
- 署名: なし

```sh
git tag -a vX.Y.Z -m "px4-userland vX.Y.Z" <result-commit>
git push origin vX.Y.Z
```

### 7. release CI の完了確認

タグの push を契機として `build_userland.yml`（release CI）が起動する。

- tag run の全ジョブが正常に成功したことを確認する（必須条件）。
- （参考: GitHub Actions では tag push に対して paths filter が評価されないため、tag run は常に起動する。一方、main ブランチへの push は paths filter があるため run の起動は保証されない。v0.1.10 や v0.2.0 で main run が起動したのは取り込んだ変更に `README.md` が含まれていたためである。main push run が起動した場合はその完了も合わせて確認する）。

### 8. tag CI アーティファクトの取得と外側チェックサム確認

タグ run で生成された `release-candidate` アーティファクトを新しい空ディレクトリへダウンロードし、外側チェックサムを検証する。

```sh
gh run download <tag-run-id> --name release-candidate --dir <tag-artifact-dir>
(cd <tag-artifact-dir> && sha256sum -c SHA256SUMS)
```

10件のアーカイブすべてに対して `OK` が返ることを確認する。main push run が存在する場合には、補助的な確認として main run 側のアーティファクトとの byte 一致も確認する（公開可否を決定する関門は工程9の候補アーカイブとの照合である）。

### 9. 候補アーティファクトとのペイロード照合（公開関門）

公開可否を決定する関門として、[`docs/release-validation.md`](release-validation.md) §2 工程5 に従い、tag run のアーカイブと工程2で実機試験した候補アーカイブを展開して比較する。

照合条件:
- 全ファイル構成（inventory）、ファイル種別（type）、権限（mode）、シンボリックリンク先が一致すること。
- 実行ファイル、共有ライブラリ、ランチャー、その他全ペイロードが byte-identical であること。
- 差分は以下のファイルのみに限定されること:
  - バイナリアーカイブ: `README.md`、`manifest.json`（`source_ref` および `README.md` のエントリハッシュ・サイズ）、内側 `SHA256SUMS`
  - ソースアーカイブ: `repository/` 配下の更新文書、トップレベル `README.md`、`BUILD-RELINK.md`（記録コミットおよびツリー行）、`source-manifest.json`（コミット・ツリーおよび該当ファイルのハッシュ・サイズ）、内側 `SHA256SUMS`

上記以外の差分が検出された場合は公開を中断し、原因を調査する。

### 10. リリースノートの準備と GitHub Release の作成（Draft）

公開用のリリースノートを準備し、GitHub Release を Draft として作成する。

- リリースノートの記述規則は [`AGENTS.md`](../AGENTS.md)「Release notes」節に従う（タイトルは `vX.Y.Z`、本文先頭見出しは `px4-userland vX.Y.Z`、概要は敬体、主な変更・検証・既知の制限は常体の箇条書き、謝辞は敬体）。
- リリースノートは日本語執筆担当が作成する。レビューで見つかった事実の誤りや規則違反は、同じ執筆担当の会話へ差し戻して修正する（新しい会話を立てずに同一の会話で直す）。
- tag run から取得した11ファイルすべてを添付して draft を作成する。

```sh
gh release create vX.Y.Z \
  --verify-tag \
  --draft \
  --title "vX.Y.Z" \
  --notes-file <path-to-notes-file> \
  <path-to-11-files...>
```

### 11. Draft アセットの再取得と照合

作成した Draft のアセットを新しい空ディレクトリへダウンロードし、アップロード内容を検証する。

```sh
gh release download vX.Y.Z --dir <draft-verify-dir>
(cd <draft-verify-dir> && sha256sum -c SHA256SUMS)
```

- ダウンロードした11ファイルが、tag run のアーティファクトとそれぞれ byte 一致することを確認する。
- チェックサム照合で10件すべて `OK` となることを確認する。
- Release 本文が準備したリリースノートと一致することを確認する。

### 12. GitHub Release の公開

Draft を解除し、正式な Latest リリースとして公開する。

```sh
gh release edit vX.Y.Z --draft=false --latest
```

公開完了後、アセットを別の新しい空ディレクトリへ再ダウンロードし、工程11と同様に byte 一致とチェックサム検証（10件 OK）を確認する。

### 13. 公開後記録の反映

公開に伴う照合証拠を [`docs/platforms/validation-results.md`](platforms/validation-results.md) の該当リリース節に記録し、main ブランチへ直接 push する。

- 記録項目: tag CI run ID、main run ID、両 run アーティファクトの一致、runner image バージョン、候補アーティファクトとのペイロード比較結果、公開日時、公開アセットの再検証結果、ローカル照合証拠の配置場所。
- コミットは main ブランチへ直接 push する。
- 公開済みのタグおよびリリースアセットは固定として維持する。
