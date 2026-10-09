# windows-cc-drop: Q3U4 8受信同時受信でのブリッジ単位TS一斉欠落の再現・比較ツール

このディレクトリは製品コードではなく、Windows版 px4-userland（`feat/windows-phase1` @ `9e2fa3e`、v0.2.0候補）で
観測された continuity(CC) 欠落を、tsukumijima/px4_drv の WinUSB 版と同条件で比較した際の
計測ツールと集計済み結果の保全です。生TSは含みません。

## 内容

| パス | 内容 |
|---|---|
| `probe/bon-ccprobe.cpp`, `probe/build.cmd` | 計測ツール。`bon` モードは BonDriver を8本開いて30秒受信、`px4ts` モードは px4-ts の stdout を受けて、同じ規則で CC 欠落・TEI・sync を数え、欠落時刻(QPC基準ms)を `probe.json` に記録 |
| `scripts/cmp-setup.ps1` | px4_drv WinUSB 版 ZIP を展開し、DriverHost INI の `DeviceInterfaceGUID` を Zadig バインドの `{11166F55-69DD-482C-A763-6126F24AAC18}` に書き換える（ドライバ・証明書の導入なし） |
| `scripts/cmp-run.ps1` | 比較実行。`-Plan` は実行順のカンマ区切り（例 `U,D0,U,D0`）。U=px4d + px4-ts×8、D0=DriverHost `DiscardNullPackets=false`（帯域を揃えた主比較）、D1=既定(`true`) |
| `scripts/redo-matrix.ps1` | E17 短時間行列試験のやり直し（世代1→カード抜去/再挿入→世代2→USB抜去/再挿入→世代3→協調停止） |
| `analysis/analyze.py`, `analysis/summ.py` | `results/cmp-main1/*/probe.json` の集計 / 1回分の詳細表示 |
| `analysis/summary.txt` | `analyze.py results/cmp-main1` の出力 |
| `results/cmp-main1/NN-<Plan>/` | 比較32回分（U×12, D0×12, D1×8、U/D0交互）の `probe.json` と px4d/px4-ts の stderr・status |
| `results/redo-d4ecb8/` | 短時間行列やり直し `redo-d4ecb8` の状態ログ（カードの ATR/APDU 応答とデバイス一覧は除外） |

デバイスのシリアルは `<BASE_SERIAL>`、ユーザー名は `<user>` に置き換えています。実行時は
`cmp-run.ps1` / `redo-matrix.ps1` の `$ID` を自分の Q3U4 のシリアルベースに戻してください。

## 実行方法（HOME-PC での例）

前提: Windows 11、Q3U4 を Zadig で WinUSB にバインド済み、`C:\px4-e17\` 配下に v0.2.0 候補 ZIP
（`archive\px4-userland-0.2.0-windows-x86_64.zip`、SHA256 `fdeb53924de621bdce0918621a8ac7c584519a7ac7949fb8d3123fd32e7ba575`）と
ファームウェア（`fw\it930x-firmware.bin`）、px4_drv WinUSB 版 ZIP（`px4_drv_winusb-260922.zip`）。

1. `probe\build.cmd` でビルド（MSVC Build Tools。`IBonDriver.h` / `IBonDriver2.h` は BonDriver SDK の
   ヘッダを同じディレクトリに置く。px4_drv 同梱のものなどで可。ライセンスの都合でここには含めていません）
2. `scripts\cmp-setup.ps1` で px4_drv 側を準備
3. `scripts\cmp-run.ps1 -Tag main1 -Plan U,D0,U,D0,...`（U/D0 を交互に。WinUSB は同時に1プロセスしか開けないため1回ずつ切り替え。結果は `C:\px4-e17\runs\cmp-<Tag>\`）
4. 集計: `python3 analysis/analyze.py <runs>/cmp-main1`

受信条件: r0/r1/r4/r5 = `isdb-s 1318000 --slot 0`（BS15/TS0, TSID 0x40F1）、r2/r3/r6/r7 = `isdb-t 527143`（22ch）、各30秒。
r0–r3 が 1台目ブリッジ、r4–r7 が 2台目ブリッジ。r7 の大量 TEI は RF/個体由来の既知バーストで別件（集計からは TEI>1000 の受信部を除外）。

## 結果の要約（cmp-main1、2026-10-10 JST）

| 条件 | 回数 | 欠落あり | 内容 |
|---|---|---|---|
| U（px4-userland） | 12 | 6 | CC誤り385件／推定欠落~1148パケット。ブリッジの4受信部の全PIDが2ms以内に同時に欠落。両ブリッジ同時4回、dev1のみ1回、dev2のみ1回、1回で最大3イベント、受信開始後6〜33秒 |
| D0（px4_drv, null破棄なし） | 12 | 2 | 開始0.5〜2秒後に低レートSI PID(0x10/0x11/0x14)で1パケット程度。起動時の性質の違う欠落 |
| D1（px4_drv 既定） | 8 | 6 | D0と同じ起動時の単発欠落のみ |

欠落率が U と同じ ~50% なら、D0 が12回連続で一斉欠落を出さない確率は ~0.02%。
同じホスト・ハブ・帯域で px4_drv では再現しないため、px4d の Windows 受信経路側の問題と見ています。
詳細と仮説は GitHub Issue を参照してください。
