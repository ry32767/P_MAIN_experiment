# 親機GNSS・LIS3MDLの更新と検証

## 前提
2026-10-10。ユーザー依頼で本番親機を更新。J_GNSS1 UART/PPS接続、GNSS・LIS3MDL通電をユーザー確認。include/pins.hを隣接AquaBeacon/pcb/build_parent_main.pyおよびbuild_child_main.pyに照合。隣接リポジトリ変更なし。USB COM6の個体番号A444A41EE9334705とP_MAIN応答で識別。Spresense本体・子機実機は変更しない。温度/水圧/LCD対象外。Wi-Fi切替・SD削除/フォーマットなし。

## 実施
時刻源を親機GNSS NMEA RMC+GP6 PPSへ変更。Spresenseのinit/pollを停止、GP7入力維持。GNSS UART速度探索・チェックサム・連続UTC条件・失効を実装。Web/API状態を更新。LIS3MDL設定書込み結果とデータレディを確認、BDUを設定。README/spec同時更新。

ホストテストでチェックサム破損、無効測位、秒未満UTC、不正日付、過長入力からの復帰、連続PPS/UTC、期限失効、無効UTC、受信遅延の拒否を検証。全既存ホストテストも実行。ビルド・書込み出力はreports/raw/2026-10-10_gnss_*に保存。

## 実測
更新前はGP6 PPSが約1秒周期、Spresense UARTパケット0、UTC未同期、地磁気null。更新後の結果は追記する。

## 未検証
絶対UTC精度、PPSとRMCの秒対応、GNSSの正確な製品型番、地磁気の校正/方位精度、距離精度、子機へのUTC配信、実機Web画面。PC Wi-Fi切替を伴うテストは実行しない。

## 更新後の実測（USB）
親機・child100ビルド成功、全ホストテスト成功。親機のみ識別付きflash.pyで記録停止後に書込み成功。20秒で21状態、全状態でGNSS同期候補と地磁気取得成立。最終状態は38400bps、NMEA 1319件、チェックサムエラー0、拒否0、使用衛星7、PPS周期999995µs、RMC受信はPPS後66858µs。RTC書込み1回、エラー0、UTC時刻2026-10-10T11:01:45。地磁気最終X=-37.8544、Y=49.9415、Z=-88.2052µT。これは取得成功で、磁気精度の合格ではない。

親機SDはCARD_NOT_PRESENT、保存行0。更新前にもSDエラーあり。既存ログは削除していない。カードを接続してのGNSS/磁気ログ保存は未検証。親機電源測定値は最終8.465V（未校正）。子機はUSB未接続、実機UTC転送未検証。

生ログ: reports/raw/2026-10-10_parent_before.txt、2026-10-10_gnss_after.txt、2026-10-10_gnss_status.json。ビルド/ホスト/書込みは2026-10-10_gnss_{build_final,host,flash}.txt。Webはオフラインで構文/既存UIテストを実行、実機画面はユーザー確認対象。

最終版再書込み後も10秒USB観測でGNSS同期候補と地磁気取得を再確認。最終生ログ: reports/raw/2026-10-10_gnss_final.txt。オフラインUIテスト成功。

最終版の測位使用衛星は8、38400bps、チェックサムエラー0、PPS周期1000004µs。最終磁気X=362.4964、Y=-115.171、Z=-478.9243µTで、Zはraw=-32768相当の飽和値。磁気値の取得は確認したが、この状態の方位用途・磁気精度は合格としない。磁石や大電流配線等から離した追加確認が必要。
