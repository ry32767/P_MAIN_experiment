# Spresense SDログ（IMU専用形式へ復元）

ユーザー指定によりIMU行へのGPS/UTC付与を撤廃した。MainCoreとSubCore1はセットで更新する。共有識別子はschema 1（0x494d5531）に戻し、形式の異なる旧SubCoreとの混在はエラーとして検出する。既存SDファイルは変更せず、新しい番号で保存する。

## IMUファイル

`Ixxxxx.CSV` は次の11列。GPS座標・GPS時刻・UTC有効フラグを含まない。

```text
seq,received_mono_us,sensor_timestamp_raw,temp,gx,gy,gz,ax,ay,az,crc32
```

960 Hz、4g/500 dps、FIFO閾値1。センサー読取り、別コアでのCSV/CRC作成、MainCoreでのSD書込みを維持する。`received_mono_us` はArduinoの電源投入後64bit micros()でありUTCではない。`sensor_timestamp_raw` は19.2 MHzの32bitセンサーカウンタ。読み出し時刻と物理的な測定瞬間は異なる。旧CLOCK_MONOTONIC版とは時計の起点が異なるため、別起動のログを直接連結しない。

IMUはSD・SubCoreの初期化後に取得を開始する。GPS待ちや固定2分・5分待機は設けない。

## GPSファイル・親機通信

`Gxxxxx.CSV` はGPS時刻・位置・状態を別に保存する。

```text
seq,received_mono_us,utc_s,nav_usec,flags,fix,satellites,lat_e7,lon_e7,altitude_mm,imu_samples,imu_errors,imu_gaps,sd_errors,sd_rows,crc32
```

GPS時刻は`utc_s * 1000000 + nav_usec`、有効性はflagsのbit0。親機へのD01 UART送信とD02のハードウェアPPS出力は維持する。Spresense自身のUTC付与用に追加したD03割り込み入力は無効化した。D03は入力のままであり、既存D02→D03ジャンパーをそのまま残しても出力同士の競合は起こさない。親機向けの配線は変更しない。

## 連続運転

GPS受信・PPS成立・監視終了で記録を停止しない。監視は`scripts/monitor_spresense.py --port COM7 --output <保存先> --seconds <秒数>`で行う。`--valid-seconds`はSpresense自身のUTC/PPS対応を確認するschema 2向け条件なので、この版の早期終了判定には使わない。

qは明示的な安全停止、rは正常停止後に新ファイルで再開、vは空き容量表示。SD/formatter異常時は再開を拒否し、異常を隠さない。sd_stateはRECORDING/STOPPED/ERROR/NOT_READYを区別する。GPS受信成功時のSD異常は、この形式復元だけで解消したと断定しない。

過去のUTC付きschema 2の詳細は[導入時レポート](../reports/2026-09-21_021_spresense_imu_utc/README.md)を参照。復元前に保存したログの追加列はそのまま残る。
