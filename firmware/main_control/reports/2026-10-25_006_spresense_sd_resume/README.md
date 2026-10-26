# FAT32カードでSpresense SD記録を再開

## 前提
- ユーザーがFAT32へ変更済みと報告し、SD記録再開を指示。
- PC Wi-Fi切替、SDフォーマット、既存ログ削除、ファーム書込みは行わない。
- 親機USBシリアル番号A444A41EE9334705とP_MAIN応答を照合し、Spresense通信が新鮮であることを確認。

## 実施
- 親機USBから `@SPR 3` を実行。command_id=1 の受付と、同一IDのlast_operation=3 / last_result=0 / command_pending=falseを照合。
- 実行成功後60秒間、親機経由でSpresenseの保存状態を監視（全取得所要72.628秒）。
- 生ログ・全状態は reports/raw/2026-10-25_006_spresense_sd_resume/ に保存。

## 実測
- recording=true、sd_ok=true、sd_state=RECORDINGで終了。終了後も記録継続。
- SD保存行数：0 → 60146。同期済み行数：0 → 59692。
- SDエラー累計：2 → 2（観測中の増加なし）。
- IMU欠落累計：2 → 2（観測中の増加なし）。
- 最終PPS対応付け=true、IMU時計準備=true。親機もsd_reason=NONEで記録継続。
- sd_failure=MOUNTは過去の失敗履歴として残っている。現在の成否はsd_ok・recording・状態・保存行数の増加で確認した。

## 未検証
- CSVファイル読戻し・全行の連番/取得時刻検査は今回行っていない。停止を伴う追加検査はせず、依頼どおり記録を継続。
- 時刻同期の絶対精度、長時間保存、物理校正の精度は今回の合格範囲外。
