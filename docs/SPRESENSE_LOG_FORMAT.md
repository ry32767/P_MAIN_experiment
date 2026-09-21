# Spresense SDログ（IMU UTC schema 2）

MainCoreとSubCore1をセットで更新する。共有メモリー識別子を変更したため、旧版との混在は記録エラーとして停止する。既存SDファイルは上書きしない。

`Ixxxxx.CSV` は従来のIMU列に次の列を追加し、最後の `crc32` は追加列も含めて保護する。

| 列 | 意味 |
|---|---|
| utc_received_us | センサーread直後の時刻をUTCに換算したUnix µs。無効なら0 |
| utc_valid | UTC換算が使用可能なら1。精度保証ではない |
| utc_sync_valid | PPSとGNSS秒の連続対応条件が成立したら1。絶対精度の合格判定ではない |
| utc_source | 0=未確定、1=GNSS通知受信からの推定、2=GNSSと直前PPSの対応候補 |
| utc_age_us | 最後の有効GNSS通知からの経過µs |
| utc_anchor_seq | 対応する `Gxxxxx.CSV` のseq |

`Gxxxxx.CSV` には `pps_mono_us` と `pps_count` を追加する。GNSS通知受信時点で観測済みの最後のPPS立ち上がりを記録する。UTC値は従来どおり `utc_s * 1000000 + nav_usec`、有効性はflagsのbit0で判断する。

## 時刻の解釈

両ファイルの `received_mono_us` とPPSは、Arduinoコア3.4.7の64bit `micros()`（電源投入後のRTC生カウンタ）に統一した。以前のCLOCK_MONOTONICとは起点が異なるため、異なる起動セッション・旧ログを直接連結しない。32768 Hzカウンタの分解能は約30.52 µs。µs単位の表示は1 µs精度を意味しない。

PPS→GNSS通知の間隔が2～500 ms、PPS周期が990～1010 ms、UTC秒とPPS番号が各1進む対応が3回連続した場合にsource=2とする。これは対応候補であり、1秒の取り違えを独立した基準時計で検証したものではない。最新GNSS通知から1.5秒を超えるか、測位・時刻が無効になるとUTCを無効にする。再同期時は時刻が段差を持つ場合があるためsourceとanchor_seqを併用する。

`utc_received_us` はIMU内部の物理的な測定瞬間ではない。SPI転送、FIFO、スケジューリング遅延は未校正。`sensor_timestamp_raw`（19.2 MHz、32bitラップ）は保存し続ける。高精度比較にはこのカウンタとPPSの追加校正が必要。source=1はGNSS通知遅延も含む。

## 追加配線

給電を切ってから、**拡張ボードのD02→D03** を短いジャンパーで接続する。D02→親機GP6の既存配線を維持し、JP1は3.3V。D03は入力専用として使い、PWM3やWire1との同時使用はしない。

| 出力 | 入力 | 用途 |
|---|---|---|
| Spresense拡張D02 | 同じ拡張ボードD03 | Spresense自身のPPS時刻取得 |
| Spresense拡張D02 | 既存100Ω経由、親機GP6 | 既存の親機同期 |

根拠：[Sony Arduinoガイド](https://developer.spresense.sony-semicon.com/development-guides/?lang=en&page=arduino_developer_guide)、[Sonyピン機能表](https://raw.githubusercontent.com/sonydevworld/spresense-hw-design-files/master/Pin/Spresense_pin_function_ja.pdf)。D03はSYS GPIO割り込み対応。接続表の構造チェックと実機での電気的確認は別に扱う。

## 確認方法

`scripts/build_spresense.ps1` で両コアをビルドし、識別済みポートに両方のSPKを書き込む。起動時に `IMU_UTC schema=2`、`SD_READBACK_OK` を確認する。測位後に `pps_count` の増加と `utc_source=2` を確認し、`q` の安全停止後、`i` / `g` でSDファイルを回収する。UTCが無効な場合もIMUデータ自体は保存する。

## 連続運転と再開

GPS受信やPPS成立で記録を停止しない。通常の監視は`scripts/monitor_spresense.py --output <保存先>`を使用し、監視が終了しても記録は継続する。旧実験のreports/raw内のスクリプトは停止qを送るものがあるため再利用しない。

明示的にqで安全停止した後は、rでGNSSを再起動せず新しいI/Gファイルに記録を再開できる。記録中のrは何もしない。未完了の停止やSD/formatter異常がある場合は再開を拒否し、異常を隠さない。sd_rowsは起動中の累計。シリアルのsd_stateはRECORDING/STOPPED/ERROR/NOT_READYを区別する。親機側の旧表示は未更新。
