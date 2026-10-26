# 親機・子機・SpresenseのSD復旧確認

## 前提

- 実施日: 2026-10-24 JST。ユーザー依頼は保存が途切れた際の3機種のSD復旧確認。追加指示により子機は未接続として扱う。
- 親機COM6 / USBシリアルA444A41EE9334705をrole=P_MAIN応答と照合してから停止・書込み。Spresenseは親機経由で応答を取得し、今回COM7を開いたりSpresenseの再起動・書込みを行ったりしていない。子機の接続・操作・書込みは実施しない。
- 配線・ピン・SPI速度は変更しない。既存のGP7–D03通信、GP6親機GNSS PPSを使用。実配線の再目視、SD媒体型番・給電波形は未確認。親機温度・水圧・LCD、音響送信、時刻・距離・センサー精度は対象外。
- PC Wi-Fi切替、実機Web操作、カード抜去・電源断・短絡、SDフォーマット、既存ログ削除は行わない。元AquaBeacon / P_MAIN_experimentには変更なし。

## 実施

1. 親子共通SdRecoveryとSpresenseSdRecoveryを同じホスト試験シナリオで検査。5/15/60秒、最大3回、短時間再発時の予算維持、30秒正常後の予算リセット、明示停止、手動再開、millis折返しを確認。
2. 明示指定の診断ビルドのみ、USB ~ の有効確認と ! の模擬障害を追加。既存ファイルを同期してから本来のfailSdを呼び、実際の待機・SD再認識・新規ファイル作成・保存を確認。カードや配線の物理的な断ではない。
3. 親機・子機の診断版をビルド。親機だけを安全停止して診断版を書込み、2回続けて模擬障害からの自動復旧を確認。復旧ログを停止後USBで全量回収し、ファイルヘッダー・各レコードのCRCと完全終端を検査。最後に記録を再開。
4. Spresenseは基準時点で実書込み障害による上限到達状態だった。親機からSTARTを1回要求して予算を手動解除し、その後に新たに発生した実書込み障害→自動復旧→保存行数・同期済み行数の再増加を観測。STOP/STARTや再起動だけを自動復旧の証拠としない。
5. 親機を通常版へ戻して、診断コマンドが応答せず通常の同期済み行数が増えることを確認。通常parent / child50 / child100の全binに障害注入の文字列が無いことも検査。

## 実測結果

| 項目 | 結果 |
|---|---|
| 親機・模擬障害 1 | P_MAIN_TIME_POSITION_00020.bin → P_MAIN_TIME_POSITION_00021.bin、自動復旧累計 2→3、同期済み3行以上 |
| 親機・模擬障害 2 | P_MAIN_TIME_POSITION_00021.bin → P_MAIN_TIME_POSITION_00022.bin、自動復旧累計 3→4、同期済み3行以上 |
| 親機・復旧後ログ読戻し | 時刻・位置4レコード、ヘルス4レコード。両ファイルのヘッダー・全レコードCRC一致、途中切れなし |
| Spresense・実障害 | WRITE / errno=110、sd_errors=10、recording=false、sd_rows=34170、sd_synced_rows=34010を観測 |
| Spresense・自動復旧 | sd_recoveries=4→5、recording=true / sd_ok=true、sd_rows=34681 / sd_synced_rows=34681。復旧処理の発動と保存再開を確認 |
| Spresense・継続性 | その後も実書込み障害が再発。最終観測は復旧累計7、SD状態ERROR（再試行上限）。安定保存の合格とはしない |
| 子機 | 未接続。共通復旧ロジックのホスト試験と50/100Hzビルドは成功。子機の実カード障害→再開は未検証 |
| 親機・通常版復帰 | 書込み成功、sd_reason=NONE。最終9秒観測中の同期済み行数0→5。診断版を残していない |

## 試験の区別・制限

親機の確認は**ソフトウェアで同期済みファイルの保存障害を模擬した実機試験**。実カード抜去、電源断、SPI信号断、カード内部の長時間停止まで再現したものではない。Spresenseは**自然発生した書込みタイムアウトからの実機自動復旧**。子機は**ソフトウェア試験のみ**。

3機種とも、障害が解消して書込み・読戻し確認に成功すると新規ファイルで記録を再開する設計。既存ファイルは保持し、障害中の欠落は埋め戻さない。復旧待機は5/15/60秒の最大3回で、認識・ファイル作成処理にかかる時間はこれに加わる。障害が続いて上限に達した場合は、媒体・接触等の確認と明示的な記録再開が必要。復旧機能が媒体・給電等の根本原因を直すわけではない。

初回の親機試験では1回目は復旧したが2回目の待ち時間25秒が不足したため、観測期限を65秒へ延長して再試験し2回とも確認。Spresenseの初期観測プログラムは障害の基準値を毎サンプル更新していたため自動判定に誤りがあり、保存済みの時系列JSONを固定した最初の障害値から再解析した。結果を再起動や手動再開だけで判定していない。

## ソフトウェア検証・再現

- 指定venvのscripts/test_host.py成功。tests/test_sd_recovery.cppを親子共通クラスとSpresenseクラスの両方へ拡張。
- parent / child50 / child100、およびparent_sd_recovery_test / child_sd_recovery_testビルド成功。
- 通常ビルドにはAQ_SD_RECOVERY_TESTが無く、障害注入処理は含まれない。診断操作は明示選択した検証ビルドに限定。
- 親機の再現手順: 診断版を個体・role照合と安全停止の後に書込み、`python scripts/verify_sd_recovery_device.py --raw reports/raw/<今回のフォルダー>`、終了後は`python scripts/flash.py parent`で通常版へ復帰。検証プログラム自体もUSB個体番号・role・診断応答を確認してから模擬障害を要求。

## 未検証

未接続の子機での実カード障害・復旧・ログ読戻し、親機の物理的なSD断、Spresense SD障害の根本原因解消と長時間安定保存、カード交換・給電/波形測定、実機Web操作、時刻・距離・IMU精度。

## 根拠

[生ログ一式](../raw/2026-10-24_005_sd_recovery/): parent_result.json / parent_test_states.json / recovered_time_position.bin / recovered_health.bin、spresense_recovery_result.json / spresense_recovery_states.json、host.txt / build_diagnostic.txt / build_production.txt / build_checks.json、upload_diagnostic.txt / restore_production.txt / final_parent.json。USB応答・位置・通信情報を含む生ログはraw内のみ保存。
