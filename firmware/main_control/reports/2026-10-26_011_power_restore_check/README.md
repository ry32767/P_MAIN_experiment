# Spresense給電後の通信・校正復元確認

## 前提
- ユーザーがSpresense未給電を確認し、給電完了を通知したため再照会した。
- 親機USB A444A41EE9334705 / COM6とP_MAIN、子機USB A521D21EB1DCFB44 / COM3とC_MAINを照合。Spresenseは親機の既存リンク経由で確認。
- USB hによる読取りのみ。書込み・再起動・校正変更・記録停止/再開・音響送信・SD削除/フォーマット・PC Wi-Fi切替なし。実機Webは操作しない。

## 実施・実測
- 約25秒の親子状態を収集。Spresense link_fresh=true、IMU補正generation=3/applied=true、保存/復元エラーとも0を確認した。
- 磁気はP_MAIN_MAGCAL_00001.binを復元しloaded=true/enabled=true。親機校正セット3番の個別校正参照も一致、復元エラーなし。
- 親機・Spresense・子機の保存件数が増加し、各SDエラー0。親子ともテザー通信・相対同期成立、通信エラー0。Spresense PPSとUTCの対応成立。
- 機体軸保存g=0、今回のカメラ用動作k=0は維持され、未実施を完了扱いにしていない。測距は停止中。

## 未検証
- 機体全体の軸合わせ、カメラを固定した動作収録と動画の時刻照合、姿勢/時刻/距離の実測精度・長時間安定性。
- 子機BNO内部の既存校正保持/精度。今回の記録進行だけでは校正合格としない。

生ログはreports/raw/2026-10-26_011_power_restore_check/power_on_P_MAIN.txt/json、power_on_C_MAIN.txt/json。保存件数の開始/終了値と検査結果はverified_summary.json。先のSpresense不通は未給電と整合し、補正データの再無効化は確認されなかった。
