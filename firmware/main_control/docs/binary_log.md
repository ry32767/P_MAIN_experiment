# バイナリ保存形式 AQBIN001

2026-10-09以降の新規SDログは `.bin`。既存CSV・Vxxxxx.BIN（512 byteカード確認用）は変更しない。テザー通信version=3・156 byteに更新したため、親子の両方の書込みが必要。

整数はlittle endian、浮動小数はIEEE754 float32、構造体のパディングはなし。ファイル形式version=1は通信versionと別。時刻の単位は列名に従い、通常は起動後のµs。UTCは別基準で、未成立は0。NaNは未取得として保持する。

## ファイルと回収

|新規ファイル例|内容|
|---|---|
|C_MAIN_IMU_100Hz_00000.bin|子機固定周期IMU、種類1|
|C_MAIN_BNO_EVENTS_00000.bin|全BNOイベント、種類2|
|C_MAIN_HEALTH_RANGE_00000.bin|子機ヘルス＋送信ごとの測距結果、種類3/5|
|P_MAIN_HEALTH_RANGE_00000.bin|親機ヘルス＋受信した測距結果、種類3/5|
|P_MAIN_TIME_POSITION_00000.bin|親機UTC・端末位置、種類4|
|C_MAIN_MULTIPATH_00000.bin|観測開始・包絡線・比較器エッジ・終了、種類6〜9|

同名の既存ファイルを上書きせず、未使用番号をO_EXCLで作成する。1秒ごとのsyncと停止時のsync/closeを継続する。Web一覧にbinと旧csvを表示し、binはapplication/octet-streamで回収する。USBのd/e/iも新規ログではbinaryを返す。USBは `# DUMP_BEGIN bytes=N` の後にN byteを読み、その後の `# DUMP_END` を確認する。バイナリ本文にマーカー文字列があっても打切らない。

```powershell
# WebまたはUSBで回収済みのファイルを検査
python scripts/decode_binary.py C_MAIN_MULTIPATH_00000.bin
# レコード種類ごとにCSVへ展開（SDの保存形式はbinary）
python scripts/decode_binary.py C_MAIN_MULTIPATH_00000.bin --csv-dir decoded
python scripts/verify_imu_log.py C_MAIN_IMU_100Hz_00000.bin --hz 100
python scripts/verify_raw_log.py C_MAIN_BNO_EVENTS_00000.bin
# USB回収は保存停止を確認し、長さ指定で読み出す
python scripts/dump_log.py --port COM3 --out child_imu.bin
```

COM番号は再確認する。上記は確認済み実機で実施するための手順で、今回の実施結果ではない。

## フレーミング

32 byteファイルヘッダ: `8s magic=AQBIN001, u16 version=1, u16 bytes=32, u8 role(1親/2子), u8 stream, u16 reserved=0, u32 boot_session, u64 created_us, u32 crc32`。CRCは先頭28 byte。

24 byteレコードヘッダ: `u32 magic=0x52425141, u16 kind, u16 payload_bytes, u64 mono_us, u32 sequence, u32 crc32`。CRCはヘッダ先頭20 byteとpayloadを連結したCRC32（標準IEEE/zlib互換）。mono_usは当該レコードの基準時刻で、ファイル内の並びが時刻順とは限らない。

|kind|内容|payload byte数|Python struct形式|
|---|---|---:|---|
|1|固定周期IMU|93|`<IIQQQQB13f`|
|2|BNOイベント|46|`<QQQIBB4f`|
|3|ヘルス／時刻／測距／設定|205|`<11I3H7f4IBIBqIIQQqI24sfQQQBIHH`|
|4|親機UTC・端末位置|34|`<QqIBBiiI`|
|5|送信ごとの測距結果|51|`<IIIIQQQqBH`|
|6|観測開始|50|`<IIIIQQQIIH`|
|7|包絡線ブロック|12+2×N|`<IIHH` + N個のu16、1≤N≤1024|
|8|比較器エッジ|5|`<IB`|
|9|観測終了|21|`<5IB`|

フィールド順の正式な定義は `common/binary_log.h`、読出し側は `scripts/binary_log.py` のFORMATS/KEYS。両者の同一性をC++生成fixtureとPythonで検証する。

kind1: index、fresh_mask、scheduled/acquired/source/UTC時刻、UTC源、qw/qx/qy/qz、加速度3軸、角速度3軸、磁気3軸。kind2: acquired/source/UTC時刻、イベント連番、sensor_id、UTC源、x/y/z/w。UTC源は0未同期/1GPS候補/2端末時計/3RTC。

kind3は `aq::Health` の全状態、link_rx、相対同期状態／offset／推定不確かさ、shot_seq、テザー受信基準／音響受信／ToF／range_state、RTC文字列と温度、UTC基準／epoch／期限／源、周波数、観測秒数／頻度。tx_usは子機のテザー受信基準で、親機の送信時刻ではない。

kind4はUTCミリ秒、端末時計経過、RTT、時刻源、位置有効フラグ、緯度経度（1e-7度）、精度（mm）。無効位置はフラグで判断し、残る座標値を有効と扱わない。

kind5は親機session、shot_seq、周波数、判定、親機送信予定、子機テザー基準、子機音響受信、ToF、観測対象フラグ、観測秒数。通信不能時は子機のログを参照する。受信RESULTのない親機側ではヘルスの取得異常も確認する。

## マルチパス観測

kind6はsession、shot_seq、周波数、sample_rate、テザー受信、最初の音響受信、ADC起動時刻、first_index、target_samples、秒数。kind7はshot_seq、絶対DMA sample_index、N、period_us=50、ADC値。ADC u16の下位12 bitが測定値、bit15がADCエラー。kind7のmono_usはブロックの先頭サンプル推定時刻で、サンプルごとに50 µsを加える。ISR/ADC起動遅延は未校正。

kind8はshot_seqとHIGH/LOW、エッジ時刻はレコードのmono_us。kind9はshot_seq、実際に書いたsample数、未保存sample数、エッジ欠落数、要求欠落数、終了理由（0完了/1手動停止/2音響受信なし/3実行不可/4SD等の取得異常）。ADC欠損は元のsample_indexを飛ばして記録し、詰めて連続データに見せない。カード抜去・電源断では終了レコードも書けない場合があり、その観測は未完了。

デコーダはCRC不一致を拒否する。電源断などで末尾だけが途中の場合、`--allow-truncated-tail` で完全レコードまで回収できるが、truncated=trueを表示する。終了レコード・サンプル数・欠損・ADCエラーを照合し、欠損のある観測をcomplete=trueにしない。

保存対象はENV_ADCの包絡線で、10〜200 kHzの搬送波を直接サンプリングした波形ではない。比較器で最初の受信を検出できなければ観測は開始しない。初期設定は受信後3秒、30送信枠に1回（最初の枠と設定変更後の最初の枠を対象）。観測中は親機が後続の音響送信を休止し、テザー状態通信は1秒ごとに続ける。3秒分は生ADCだけで120000 byte、ブロック等の付帯情報は別。実際のSD帯域・ADC時刻精度・マルチパス識別能力は未検証。

ADC DMAは既存の32 KiBゲイン確認バッファを排他的に共用する。ゲイン確認は子機SD停止時のみ。観測中はボタンADC／通常ENV読出しを止めるためその状態値は直前の値になり、漏水デジタル入力・IMU・I2C取得は継続する。

親機が子機SD正常を確認できない送信枠は観測対象にしない。ゲイン確認中のSD停止では観測用の送信休止を挿入しない。対象選択後にSDが停止・故障した場合は子機が実行不可を記録できる範囲で報告する。

## 2026-10-10の互換拡張

通信はversion=4・158 byteへ更新。種類3（HEALTH）と5（SHOT）のpayload末尾に `u8 ranging_rate_hz, u8 acoustic_pattern` を追記。ファイルversion=1とレコードCRCは維持する。更新したbinary_log.pyは旧長さも受理し、旧レコードは1回/秒・単発として読む。詳細は[仕様](spec.md)の測距回数・簡易音響パターンの節を参照。

## 2026-10-10 可変パターンの追加

通信v5・163 byte。HEALTH/SHOT末尾のrate/patternの後ろに `u8 acoustic_pattern_id, u16 pattern_gap1_us, u16 pattern_gap2_us` を追加（little endian）。ID=255は単発／予約なし。binary_log.pyは元の長さ・rate/patternのみの長さ・今回の長さの3種類を受理する。旧ログのID／休止時間はnull。ファイルversion=1とCRCは維持。SHOTにはその測距枠の情報を保存する。ヘルスのID／休止時間は直近予約の情報で、設定を変更した直後は設定モードと直近IDが異なる場合がある。詳細はspec.mdの「毎回変わる音響パターンとテザー通知」を参照。

## 2026-10-25 親機観測・校正セット

新規kind10 MAG_OBSERVATION（84byte）はlittle endian `<IQQQIIIHH9fI`：取得連番、read開始/終了µs、参考UTCµs、個別校正番号、親機セット番号、時刻不確かさ（UINT32_MAX=未測定）、時刻源u16、有効u16、生XYZ・補正XYZ・機体軸XYZ、機体軸有効u32。機体軸未設定はNaN、有効0。DRDYそのものの時刻ではない。

kind11 CALIBRATION_SETは376byteの親機セット版1（magic=PSET）。先頭`<IHHIIIIQI`はmagic/版/サイズ/セット番号/保存時親機session/IMU校正番号/磁気校正番号/保存時UTC/時刻源。続く40byte×6はセット名/CAD版/機体名/IMU識別/磁気識別/GNSS識別。各32byte×3の取付情報は`<I7f`（確認済み、位置XYZ[m]、センサ→FRD四元数wxyz）。末尾CRC32は前372byteを対象とする。親機ログ新規開始時に複製し、デコーダは内外CRCを検査する。

kind12 GNSS_OBSERVATION（64byte）は`<IQQ4B6i4I`：iTOW[ms]、受信完了µs、参考UTCµs、時刻源/fix/flags/flags2、緯度経度[e-7度]/楕円体高[mm]/速度NED[mm/s]、hAcc/vAcc[mm]/sAcc[mm/s]/セット番号。レコードヘッダーsequenceは起動中NAV-PVT観測連番。iTOWが受信機の観測エポックで、参考UTCは受信時刻への対応。

従来kind1〜9とフレーミング/CRCは維持。`python scripts/decode_binary.py <file> --csv-dir <new-directory>`で新旧を種類別に展開できる。Spresense CSV schema3は[親機校正セット](parent_calibration.md)参照。

kind10の有効u16は下位bit0が値の有効性、上位8bitがLIS3MDL STATUS_REGの保存値。デコーダは`valid`をboolへ分離し、`hardware_status`と`hardware_overrun`（bit7 ZYXOR）を追加する。旧kind10は上位8bit=0として読めるが、状態未記録と上書きなしの実測を混同しない。詳細は[STデータシート](https://www.st.com/resource/en/datasheet/lis3mdl.pdf) §7.10。


## 機体校正の収録・変換（2026-10-26）
- kind13 BODY_EVENT、260byte。`u32 capture,boot,imu,mag; u64 mono,utc; u32 sequence,drops,uncertainty; u8 event,phase,utcSource,mask; char device[20],log[64],raw[64],health[64]`。eventは1開始/2区間開始/3区間終了/4完了/5中止。UTC不成立0、uncertainty=UINT32_MAX（未測定）。BODY_CAPTUREファイルに独立保存し、通常ログの時間区間を参照する。
- kind14 BODY_PROFILE、112byte。`u32 magic=BODY; u16 version=1,bytes=112; u32 generation,capture,boot,imu,mag; u64 utc; u8 utcSource,axes,camera,reserved; char device[20]; f32 imuQ[4],magQ[4]; f64 cameraScale,cameraOffsetUs; u32 crc32`。内部CRCは末尾4byte以外へ適用。Qはwxyz、センサ→機体FRD。axes bit0=IMU、bit1=磁気。cameraの変換は元の収録/クリップ専用で、普遍的なUTC精度を表さない。
- CAMERA_CAPTURE_*.binもkind13/260byteを使用する。開始event6は保存済み軸を再利用するカメラ専用収録、区間5だけを必要とし完了mask16。BODY_CAPTUREの開始event1/完了mask31は維持。CAMERA_CAPTUREの連番はBODY_CAPTUREと共有するが、カメラ専用収録で保存軸を無効化しない。状態/プロファイルのcameraフラグは今回の必須収録の代わりにはならない。
- ALIGN/BNO_CALファイルはプロファイル自体を保存し、各通常ログで個別校正の一致とセンサ状態を確認できた時に、適用プロファイルをkind14で一度複製する。既存kindとレコードCRC、AQBIN001版1は維持。binary_log.py/decode_binary.pyで新旧を読める。
- 詳しい操作、校正参照の失効、開始警告は[機体校正](body_calibration.md)。
