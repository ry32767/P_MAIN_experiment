# 親機GNSS RTK・PPK更新

## 前提
2026-10-10。ユーザー依頼: RTK/PPK対応、親機コンソールで基準局選択/自動選択、PPKデータ保存。既存J_GNSS1配線、GP6 PPS、GP8/9 UART1、Spresense未変更。隣接AquaBeacon/pcb/build_parent_main.py、build_child_main.pyにピンを照合（前回検証と同じ配線）。元リポジトリ変更なし。書込みはUSB個体番号A444A41EE9334705とP_MAIN応答でCOM6を識別、記録停止後実施。SD削除/フォーマットなし。PC Wi-Fi切替・実機Web自動テストなし。

## 実施
MON-VERによるF9P識別、RAMのみVALSET/VALGET設定、生観測/航法/PVTのUBX受信。RTK解表示、NTRIP v1接続・STR一覧・最寄り候補・GGA・CRC24Q検査・UART補正転送・再接続。コンソールの接続/基準局/アンテナ設定とPPK状態。SDに新規ubx/rtcm3/jsonl保存、停止/同期/再開・Web/USB回収。検査/RINEX変換補助スクリプト。README/spec/docs索引更新。

ホストテスト成功: UBXチェックサム/最大長/過長/復帰、RTCM CRC24Q標準ベクトル/破損/予約bit、STR候補/不正座標/名前、距離計算。他の全既存ホストテストも成功。親機ビルド・書込み成功。オフラインコンソール構文/ラベル/構造テスト成功。Arduino-Pico UART availableForWriteが0/1であることを確認し、固定長一括送信条件からhardware writableを確認する分割送信に修正。NTRIP TCP/DNSは短時間の同期待ちが残るがUARTはcore1で受信する。

## 実測
MON-VER: MOD=NEO-F9P、FWVER=HPGL1L5 1.40、PROTVER=27.40。230400bps、設定ACKあり、VALGETの6設定一致。PPS/UTC候補継続、UART overflow0、UBXエラー0。初期速度探索時のNMEAチェックサムエラー1あり。12衛星使用、RTK解はsingle（補正未設定）。hAcc推定約37mで精度合格ではない。

親機SD正常。P_MAIN_GNSS_00002.ubx/rtcm3/jsonlを作成して保存し、USB停止/回収。UBX18022byte、RAWX22エポック、SFRBX32メッセージ、NAV-PVT22、MON-VER1。PC再検査で全UBXチェックサム一致、観測間隔1秒、異常間隔0。各先頭/末尾エポックの観測信号18/17、搬送波有効6。GPS週2439、TOW565580.987〜565601.987。記録キュー欠落0。JSONL7040byte、RTCM0byte（未接続）。回収後に親機の記録を新規ファイルへ再開した。

## 証拠
reports/raw/2026-10-10_rtk_{host,ui,flash,flash2,flash3,final_status}.txt、final_status.json、rover.ubx、rover.rtcm3、rover.jsonl、ppk_audit.json。生ログと位置情報はreports/raw下に保存。

## 未検証
NTRIP実サービス接続、一覧の実取得、GGA/RTCM実転送、RTK Float/Fixed、RTK位置精度、RINEX変換、基準局データを併用したPPK解算、長時間・補正/測距/SD同時負荷、実機Web画面、子機UTC転送。サービス未指定なので実補正の接続先/基準局は設定していない。Spresense・親機温度/水圧/LCD対象外。自動最寄りはRTCM3/2周波候補の距離選択で、L5配信有無・精度・権限を保証しない。

## 最終版確認
親機/child100の最終ビルド成功、子機への書込みなし。NTRIP一覧取得の再試行時に一覧をクリアする修正を含む最終親機を識別付き再書込み。20秒USB確認でconfig_verified=true、PPK記録中、RAWX/SFRBX保存増加、UART overflow/UBX errorsとも0、UTC候補成立を確認。現在ファイル P_MAIN_GNSS_00005.ubx へ継続記録。生ログ: reports/raw/2026-10-10_rtk_final_recheck.txt、書込み: rtk_flash_final.txt。JSONL7レコードのJSONと保存数をPCで検査し、認証/SSID/passwordキーがないことを確認。
