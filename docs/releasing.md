# Stable リリース手順

この文書は px4-userland の候補固定、検証、リリースノート作成、タグと GitHub Release の公開手順です。判定条件の正本は `SPEC.md` 10章、実機手順は [release-validation.md](release-validation.md)、記録形式は [release-record.md](release-record.md) を参照してください。この手順の実装・PR・CI 成功は、実機合格や公開の承認ではありません。

## 共通の順序と利用者の判断

- **A: 合格した場合** — candidate → 試験結果の文書コミット → 依頼後にノート作成・コミット → 両方を含むコミットに、別の依頼後にタグ → 自動照合・Release 公開。
- **B: 失敗してコード修正が必要な場合** — 失敗を記録 → コード修正 → 新 candidate → 必要な検証を繰り返し、合格してから A へ進む。修正前の候補の合格や旧 hash を新候補へ流用しません。

候補前の placeholder はファイルの置き場所を確保する準備だけです。ノート本文や試験結果を先に書くものではありません。

1. 候補を固定し、CI の候補アーティファクトを検証します。
2. 必要な実機試験・仕様変更試験・ユーザーが決めた soak と再現性確認を完了します。
3. 試験結果を文書へ記録してコミットします。
4. 利用者から明示的に「リリースノートを書いて」と依頼された後、利用者向けのノートと機械可読記録を確定し、文書だけの後続コミットにします。
5. 利用者から別途「タグ＋リリース」の依頼を受けた後、試験結果とノートの両コミットを含むコミットに注釈付きタグを作成して push します。
6. タグを契機に、タグ CI の成果物を照合し、Draft の添付確認、公開、公開後確認まで workflow が実行します。

試験完了、ノート作成の依頼、ノートのコミットは「タグ＋リリース」の許可を代替しません。タグ push が公開工程を開始するため、承認前にタグを push しません。コミット・push・マージも、その操作についての利用者の依頼に従います。追加の GitHub Environment 手動承認は設けません。

## 公開アーティファクト

公開するのは `build_userland.yml` の成功した **tag push run** が生成した `release-candidate` です。実機試験に使った候補 run とは別の run であり、候補をそのまま添付する手順ではありません。

- バイナリアーカイブ 9 本
- `px4-userland-<version>-source.tar.gz` 1 本
- 外側 `SHA256SUMS` 1 本（10 アーカイブの SHA-256）

合計 11 ファイルです。具体的な target は [release-validation.md](release-validation.md) の配布 matrix を正本とします。Windows 11 x64 の ZIP を含む 9 binary target が必須です。APK・FreeBSD・firmware は配布しません。

## 1. 候補の準備と固定

対象の `VERSION` とブランチ（現在は `main`）を確認します。バージョンを変更する判断は別途行い、この手順の追加だけでは変更しません。

候補を作る前に、対象版の `release-notes/vX.Y.Z.md` と `release-notes/vX.Y.Z.json` を未完成の置き場所として用意します。[release-notes/README.md](../release-notes/README.md) の無効な placeholder を使い、変更内容・合格・承認を先回りして書きません。ソースアーカイブの inventory も照合するため、これらのファイルを試験後に新規追加せず、最初から候補へ含めます。

候補の full source SHA を固定し、`build_userland.yml`（`portable userland foundation`）を実行します。この build workflow 自体はタグや Release を作成しません。

```sh
set -eu
: "${CANDIDATE_REF:?export the exact candidate source ref}"
: "${ARTIFACT_DIR:?export a new empty artifact directory}"
case "$CANDIDATE_REF" in ''|*'<'*|*'>'*) echo 'invalid or unreplaced candidate ref' >&2; exit 2 ;; esac
case "$ARTIFACT_DIR" in ''|*'<'*|*'>'*) echo 'invalid or unreplaced artifact directory' >&2; exit 2 ;; esac
test ! -e "$ARTIFACT_DIR" || { echo 'artifact directory must be new and empty' >&2; exit 1; }
gh workflow run build_userland.yml --ref "$CANDIDATE_REF"
gh run list --workflow build_userland.yml --limit 10
: "${RUN_ID:?after confirming successful run, export its exact numeric ID}"
case "$RUN_ID" in ''|*[!0-9]*) echo 'run ID must be decimal' >&2; exit 2 ;; esac
mkdir -m 700 "$ARTIFACT_DIR"
gh run view "$RUN_ID" --json headSha,event,conclusion
gh run download "$RUN_ID" --name release-candidate --dir "$ARTIFACT_DIR"
if command -v sha256sum >/dev/null 2>&1; then
  (cd "$ARTIFACT_DIR" && sha256sum -c SHA256SUMS)
else
  (cd "$ARTIFACT_DIR" && shasum -a 256 -c SHA256SUMS)
fi
```

実際の ref/run ID/空ディレクトリを使います。実機試験に使う archive 名・hash・source SHA・run ID/attempt を固定し、ローカル build や別 run へ置き換えません。CI の全必須 job、archive audit、source/relink、各 platform smoke が成功していることを確認します。

## 2. 実機試験・仕様変更試験と再現性

[release-validation.md](release-validation.md) の既存条件で、配布する 9 binary archive を各 OS/architecture の実機で確認します。model、host、access path、feature は独立の軸です。他機種・他 OS・他 ABI の成功や過去の候補で穴を埋めません。物理 USB/card/アンテナ/電源操作は利用者が行い、承認された範囲だけ試験します。

同じ source SHA/tree の独立した 2 回の clean candidate run を取得し、10 アーカイブ全体と外側 `SHA256SUMS` が byte-identical であることを確認します。run ID は別々でなければなりません。実効 build input/toolchain と runner image の観測値を両方残します。runner image が異なっても実効 build input の一致を確認できればよく、入力不明や入力差は合格にしません。UUID・署名・timestamp 等をマスクした比較で代替しません。

証拠の本文は [既存の実測記録](../docs/platforms/validation-results.md) へ日付付きで追記します。失敗試行、未認定、対象外、継承の根拠を残し、機械可読記録の `completed` を実測の代わりにしません。必須条件が未完了ならここで止めます。

## 3. 利用者の依頼後にノートと記録を確定

全必要試験を完了し、試験結果の文書コミットを作った後、利用者がノート作成を依頼した場合に限り、`release-notes/vX.Y.Z.md` の placeholder を利用者向け Markdown に置き換えます。ノートは後続の別コミットにします。タイトル・本文の書式は `AGENTS.md`「Release notes」に従います。内部監査ログや matrix を本文へ複製しません。事実誤りや書式違反があれば同じ執筆担当へ差し戻して直します。

`release-notes/vX.Y.Z.json` には [記録形式](release-record.md) に従って、ノートの SHA-256、候補と再現性確認の run 情報、全アセットの hash、実機・仕様変更試験・soak 判断・support 表示・公開前レビューの証拠を記録します。証拠が揃わない gate は `completed` にしません。soak の実施有無・時間・対象の判断は利用者のものであり、workflow はその妥当性や物理測定を自動認定しません。

許可された結果コミットを対象ブランチへ取り込み、候補からの差分が文書だけであることを確認します。ソースのファイル構成・type・mode・symlink target を維持し、コード、script、workflow、build input、firmware を変えません。機械可読記録やノートも source inventory の一部です。これらを追加し忘れた場合は inventory の関門を緩めず、新 candidate と必要な再確認へ戻ります。

## 4. 別の「タグ＋リリース」依頼後にタグを push

利用者が対象版と結果コミットについてタグ付け・公開を依頼した後だけ、未使用の `vX.Y.Z` を作成します。以下の `<result-commit>` は試験結果コミットと後続のノートコミットの両方を含む最終コミットです。`VERSION` とタグの版を一致させ、既存タグを移動・上書きしません。

```sh
set -eu
: "${CANDIDATE_COMMIT:?export the fixed candidate commit}"
: "${RESULT_COMMIT:?export the final result and release-notes commit}"
: "${VERSION:?export the confirmed release version}"
for value in "$CANDIDATE_COMMIT" "$RESULT_COMMIT" "$VERSION"; do case "$value" in ''|*'<'*|*'>'*) echo 'replace all release values before continuing' >&2; exit 2 ;; esac; done
git diff --stat "$CANDIDATE_COMMIT" "$RESULT_COMMIT"
git tag -a "v$VERSION" -m "px4-userland v$VERSION" "$RESULT_COMMIT"
git push origin "v$VERSION"
```

タグは注釈付き・署名なしです。タグ push により `build_userland.yml` の tag run と、公開用 `release_tag.yml` が始まります。通常の branch/PR CI や `workflow_dispatch` は Release を公開しません。

## 5. 自動照合、Draft、公開

`release_tag.yml` は PX4 側の共通 `release_reusable.yml` を使用します。ASICEN から使う共通 workflow/helper は full commit SHA に固定し、浮動 branch/tag を参照しません。公開されている PX4 の参照に新しい token や Environment 承認を要求しません。

共通 workflow は、同じ repository、正確な tag SHA/ref、`build_userland.yml`、`push` に対応した成功 run を確認します。候補/再現性 run についても repository、workflow、SHA、attempt、成功状態と実アーティファクトを照合します。run 未完了・失敗、artifact 期限切れ・欠落、入力不一致は公開を止める理由です。

照合では、候補 2 run の完全 byte 一致と、候補対 tag archive の展開後 inventory/type/mode/symlink target を確認します。実行ファイル・共有ライブラリ・ランチャー等の文書以外の payload は byte-identical が必須です。許容差は更新文書、文書と commit/tree に由来する明示的な manifest 項目、内側 checksum に限定します。未知の metadata 変更や未分類の差を見逃す allowlist は追加しません。

最初の検証 job は read-only です。公開 job だけが `contents: write` を持ち、再照合してから以下を実行します。

1. `vX.Y.Z` をタイトル、コミット済み Markdown の正確な内容を本文とする Draft を作成し、tag run の 11 アセットを添付
2. Draft から新しい空ディレクトリへ再取得し、全ファイルの byte 一致、10 件の checksum、タイトル・本文を確認
3. Draft を解除して Stable/Latest として公開
4. 公開済みアセットを別の空ディレクトリへ再取得し、同じ照合と Latest 状態を確認

既存の同一 Release は一致を確認して再実行を継続できますが、異なる本文・アセット・タグを上書きして合わせません。不一致では停止し、原因と現状を報告します。

## 6. 結果の確認と記録

公開 workflow の成功だけでなく、その出力から tag SHA、candidate/reproduction/tag run ID、inventory/hash、Draft/公開後照合、Release URL と Latest 状態を確認します。公開後照合が失敗した場合は「公開されたが検証未完了」と区別し、成功と報告しません。公開済みタグやアセットを修正のために置き換えません。

公開日時、照合結果、ログの所在を [実測記録](../docs/platforms/validation-results.md) に追記し、commit/push は利用者の依頼に従います。この後続記録のためにタグを移動せず、公開物は固定します。
## 7. v0.2.1 の限定 recovery entry point

v0.2.1 は注釈付きタグと product commit を変更せず、タグ push 後に検証 job で停止した既知の失敗だけを復旧します。対象は次の固定 identity です。

- tag: `v0.2.1`
- annotated tag object: `4939fdec32900a807252586e807e97f19e6b6612`
- product commit: `dd1c1bea16a5871085d5f2a42c009364c1ec7345`
- failed publisher push run/attempt: `38330456338` / `1`
- successful tag-build push run/attempt: `38330455992` / `1`
- failed verification report artifact: `11782910579`
- tag-build `release-candidate` / `release-provenance` artifacts: `11782756340` / `11782641618`

独立レビュー後に利用する entry point は `.github/workflows/release_recovery.yml` の
`workflow_dispatch` のみで、ref は `main` に固定します。入力は上記の tag、annotated
tag object、2 つの元 run ID/attempt だけです。workflow と helper は指定値が固定 identity と
一致すること、実際の dispatch event、run attempt、workflow path、非 fork repository ID、
元 verifier の `publication_attempted=false` / `release_id=null` / `verify-inputs` report、
verification failure と publication skip、成功した 20-job tag build とその artifacts を
GitHub API と artifact digest で照合します。artifact は JSON evidence としてのみ読み、
実行しません。

workflow checkout の `GITHUB_SHA` / `GITHUB_WORKFLOW_SHA` は recovery tooling の revision
です。product SHA は tag object から別に解決して上記 commit と照合します。read-only verify
job と contents-write publish job は両方とも provenance と全 release gate を再検証し、
通常の tag-push verifier と同じ concurrency group を使います。既存の `release_tag.yml`
tag-push flow は変更しません。

この手順ではタグを再作成・移動・削除せず、Release を手動で作成・upload しません。通常の
公開 workflow が Draft、公開、Latest、公開後の再取得検証を担当します。どの identity、
attempt、artifact、payload 検証でも一致しなければ停止し、workflow dispatch せずに原因を
独立レビューへ戻します。
