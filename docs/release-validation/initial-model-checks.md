# PX4 初回機種チェック

この初回手順はPX4の対象全機種に適用する。各機種について代表Linux x86_64 glibcで2時間連続受信し、その機種で毎release回帰項目と必要時項目を実施する。機種ごとに結果を分け、他機種の結果を継承しない。通常の無指定時検証機種はPX-Q3U4だが、初回対象の限定ではない。以下はPX-Q3U4の問題発見段階の実績で、最終candidate archiveの初回認定ではない。

## PX-Q3U4 実測結果

| 項目 | 実施内容と結果 | 状態 |
|---|---|---|
| 2時間連続受信 | Linux x86_64 glibc上のlocal build-qaでPX-Q3U4を受信。受信統計・条件は1.0.0の内部検証記録に記録。 | 実施済み、final candidateでは未認定 |
| 全channel短時間取得 | 地上波/無料衛星targetを各10秒取得。channel実値は試験時の現地planに従った。 | 実施済み、final candidateでは未認定 |
| 最大受信 | 8 receiverの地上/衛星同時受信を実施。 | 実施済み、final candidateでは未認定 |
| glibc配布candidate 8 receiver回帰 | artifact `12583631708`で8 receiverを30秒同時実行。client各exit 0/counter 0、stream中status 8/8 streaming。独立TS解析はreceiver 0–6で大量TEIと複数receiverのcontinuity discontinuityを検出し、client counterと不一致。受信中APDU 10回は全てexit 5/responseなし。通常daemon stop/cleanupはexit 0、process/FD/socket残留なし。 | 最大同時起動/終了は実施、TS品質と反復APDUは不合格。receiver 7 reference比較は未実施。初回認定未達 |
| CARD / APDU | CARD queryを実施。独立した初回PC/SC probeはB-CAS initializationに失敗。 | 部分実施、PC/SC未達 |
| LNB | software制御と端子実測で0→15→0Vを確認。 | 実施済み |
| USB切断・cleanup・復帰 | 受信中抜去後52.486秒でclient/daemonが自然終了せず、statusが残った。通常SIGINTではclient exit 7、daemon exit 70 / INTERNAL cleanup NOT_READY。停止後の再接続、新daemon起動、短いT27受信は成功。 | 未達。切断中の有限終了とcleanup未達 |
| OS/ABIのnative回帰 | 最終candidate archiveによる全配布OS/ABI確認なし。 | 未実施 |

PX-M1URとPX-S1URには過去の機種別試験記録があるが、本書の初回手順を完了した結果ではない。両機種の初回結果は未記録であり、未実施項目を初回認定済みにしない。今後追加される機種も同じ共通手順を実施し、独立した結果を追記する。PX4 1.0.0の問題発見試験は[`1.0.0.md`](1.0.0.md)に記録する。
