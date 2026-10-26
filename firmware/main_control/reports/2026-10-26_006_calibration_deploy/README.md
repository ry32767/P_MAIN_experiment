# 校正中フィードバック・校正順案内の実機配備

## 前提
- 2026-10-26 JST。ユーザーが実機書込みを指示。
- 親機USB A444A41EE9334705 / COM6とP_MAIN応答、Spresense USB 10878C89E6DBED1197F5F12D62C613AC / COM7とIMU_TIME・LINK・PPS入力D02のロガー応答をそれぞれ照合。
- 子機USB A521D21EB1DCFB44は未接続。親機からも子機応答なし。子機は書き込まない。
- 配線変更なし。GP6=外付けGNSS PPS入力、GP7–D03=既存半二重、Spresense D02=PPS入力専用を維持。親機温度/水圧/LCDは検証対象外。
- PC Wi-Fi切替、実機Web自動操作、SDフォーマット・既存ログ削除、元リポジトリ変更なし。

## 実施
1. 書込み前のUSB状態を保存。親機SDはNONE・記録中、Spresense通信あり・記録停止、親機セット番号1。未保存の進行中IMU校正がないことを確認。
2. 親機へXqを送り、測距停止・SD STOPPED・記録フラグ解除を応答で確認。Spresenseも個体番号/ロガー応答を再確認し、qに対するSTOPPED_REMOVE_SDを確認。
3. 最新ソースでparent/child100、Spresense formatter/loggerをビルドし成功。書込み対象のSHA256を保存。
4. Spresense MainCore/loggerを公式Arduino CLI、UploadSpeed=921600で更新。Package validation is OK、nuttx保存・再起動を確認。再起動後のロガー応答を確認し、qで再び安全停止。
5. scripts/flash.py parentでUSB番号とP_MAIN応答を再照合し、記録/測距を安全停止して親機を書込み。フラッシュ照合100%・SUCCESS・再起動を確認。
6. USBから親機の状態とWeb待受け診断を取得し、新しいIMU診断・磁気評価フィールド、ログ件数増加、校正セット復元、Spresense停止を確認。ビルド/書込み後の対象ハッシュ一致を確認。

Spresense SubCore1/formatterの共有ABI・処理は今回変更していない。両コアをビルドしたが、今回は変更されたMainCore/loggerのみ書き込んだ。子機BNOの案内は親機が配信する画面側の変更で、子機ファームの変更はない。

## 実測・結果
- Spresense：220400byteのパッケージ転送、検証成功、nuttx保存・再起動。
- 親機：フラッシュ照合100%、SUCCESS。
- 親機再起動後、最初のUSB接続15秒ではP_MAIN状態応答0件。ポートを閉じて再接続した30秒確認では55件。追加のリセットや再書込みは行っていない。初回無応答の原因は未確定で、前回配備にも同様の記録がある。
- 最終USB状態：Spresense link_fresh=true、imu_ok=true、calibration_diagnostics=true、拒否理由0/回数0。新しいロガー診断を親機が受信できることを確認した。校正測定は実施していない。
- 親機磁気のfit_evaluated/radius_matchフィールドが存在することを確認。実機磁気校正を行ったものではない。
- 親機SD reason=NONE、記録中、SDエラー0。確認区間の行数0→10、PPK RAWX保存0→12。PPK記録中、測距停止。
- 親機校正セット番号1を復元。これは個別校正の完了・精度合格ではない。
- Spresenseは記録停止、SD state=STOPPED / failure=NONE / detail=0。再起動後の停止状態であり、以前のSD障害の解消や連続保存成功を証明しない。
- Spresense PPS検出・UTC対応・IMU時刻推定が成立。時刻精度・距離精度は未測定。
- 親機Web listening=true。実機Webページの表示・操作成功とは扱わない。

## 未検証
- スマホでの実機校正画面、実機6姿勢/全方向校正と保存、実際の動きに対する拒否理由の画面変化。校正済みと判断できる実測精度の検証。
- 未接続子機の操作、Spresenseの連続SD保存・読戻し、長時間安定性。
- 初回USB無応答の原因、カメラIMUとの時刻照合と精度。

ユーザーは親機コンソールを再読み込みし、センサー校正で順序案内と条件表示を確認する。PCのWi-Fiを切り替える必要はない。

## 証跡
reports/raw/2026-10-26_006_calibration_deploy/：identification.json、parent_before.json/txt、spresense_before.txt、parent_safe_stop.json/txt、spresense_safe_stop.txt、build.txt、build_spresense.txt、deployment_hashes.json、upload_logger.txt、logger_after_safe_stop.txt、upload_parent.txt、after.json/txt、after_summary.json、web_diag.json、verify_after.py。

ソフトウェア変更とホスト試験の詳細は[前回レポート](../2026-10-26_005_calibration_feedback/README.md)。既存ログ・校正ファイルの削除や上書きは行っていない。
