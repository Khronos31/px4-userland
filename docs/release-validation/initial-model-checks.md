# PX4 初回機種チェック

標準機種はPX-Q3U4。確定条件は代表Linux x86_64 glibcで2時間連続受信し、同機種で毎release回帰項目と必要時項目を実施すること。以下は問題発見段階の実績で、最終candidate archiveの初回認定ではない。

| 項目 | 実施内容と結果 | 状態 |
|---|---|---|
| 2時間連続受信 | Linux x86_64 glibc上のlocal build-qaでPX-Q3U4を受信。受信統計・条件は1.0.0の内部検証記録に記録。 | 実施済み、final candidateでは未認定 |
| 全channel短時間取得 | 地上波/無料衛星targetを各10秒取得。channel実値は試験時の現地planに従った。 | 実施済み、final candidateでは未認定 |
| 最大受信 | 8 receiverの地上/衛星同時受信を実施。 | 実施済み、final candidateでは未認定 |
| CARD / APDU | CARD queryを実施。独立した初回PC/SC probeはB-CAS initializationに失敗。 | 部分実施、PC/SC未達 |
| LNB | software制御と端子実測で0→15→0Vを確認。 | 実施済み |
| USB切断・cleanup・復帰 | 受信中抜去後52.486秒でclient/daemonが自然終了せず、statusが残った。通常SIGINTではclient exit 7、daemon exit 70 / INTERNAL cleanup NOT_READY。停止後の再接続、新daemon起動、短いT27受信は成功。 | 未達。切断中の有限終了とcleanup未達 |
| OS/ABIのnative回帰 | 最終candidate archiveによる全配布OS/ABI確認なし。 | 未実施 |

初回チェックは全項目が完了するまで未認定とする。PX4 1.0.0の問題発見試験は[`1.0.0.md`](1.0.0.md)に記録する。
