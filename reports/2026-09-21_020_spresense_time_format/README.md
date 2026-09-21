# Spresense保存形式の時刻確認

- 日時：2026-09-21 JST。
- 目的：GPS時刻が保存されているか、IMUにもUTCが付いているかを確認。
- 方法：公開ソースf0871b1のSpresenseコードと、レポート016で以前回収したGPS CSVを読み取り。今回の新規実機取得・書込み・配線変更なし。ローカルコードは改行を正規化すると公開コードと一致。

## 前提・接続

対象はSpresense内蔵GNSS、SPI5 Multi-IMU、拡張ボードSDHCI。既存構成はD01 UART TX→100Ω→親機GP7（物理10）、D02 PPS→100Ω→親機GP6（物理9）、共通GND。今回は接続状態・電圧・SD型番を再確認していない。Pico、子機、温度・圧力・RX・LCD等は操作対象外。Pythonで既存CSVを集計した。

## 結果

| ログ | 保存される時刻 | 判定 |
|---|---|---|
| Gxxxxx.CSV | utc_s（Unix UTC秒）、nav_usec（端数µs）、flags（bit0が時刻有効） | GPS UTCは保存されている |
| Ixxxxx.CSV | received_mono_us（起動後の読取り時刻）、sensor_timestamp_raw（センサー生カウンタ） | IMU行にはUTC列・UTC同期有効フラグがない |

GPS UTCのµs値は `utc_s * 1000000 + nav_usec`。有効時刻として扱うのは `(flags & 1) != 0` の行だけ。既存回収GPS CSVは54行中20行が時刻有効。緯度経度等の生データは公開せず、[集計](evidence/results.json)を残す。

両CSVのreceived_mono_usは同じCLOCK_MONOTONICに基づくため、同じ起動セッションであれば事後の概略対応付けには使える。しかしGPS側はgetNavData後、IMU側はセンサーread後の受信時刻であり、遅延・揺らぎを含む。GPS UTCとこの受信時刻の差をそのまま使っても、IMU測定瞬間の高精度UTCを保証しない。センサー生カウンタのラップ・周波数・対応付けも考慮が必要。

レポート019で追加したUTC列はPico親子統合ファームウェアのログが対象であり、Spresenseのファイル形式は変更していない。GPS時刻が一切記録されていない状態ではないが、Spresense IMUを親子ログと高精度に比較する用途には不足が残る。

## 未実施・必要な改善

SpresenseのIMU取得時刻とGNSS/PPSを対応付け、各IMU行にUTCと有効性・時刻源を記録し、未測位/失効時は無効表示する必要がある。単なる受信時刻差による換算を精密同期として扱わない。今回の確認ではファームウェア変更・ビルド・実機試験はしていない。
