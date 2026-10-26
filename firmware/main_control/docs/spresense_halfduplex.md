# Spresense 1線半二重通信

## 配線と適用範囲

| 親機 | Spresense拡張ボード |
|---|---|
| GP7 / J_SPR1.2 | D03 / JP13.5 |
| GND / J_SPR1.3 | GND |
| GP6 | 親機GNSS PPS専用。Spresenseへの配線なし |

ユーザー申告: JP1=3.3V、追加4.7kΩなし、GND共通、D02未接続。公式回路図ではD03にRN3の1kΩプルアップがありLSF0108で1.8/3.3Vを変換する。物理実測は未実施。D00/D01/D02をこの通信へ混ぜない。旧ファームウェアはD03をプルダウン入力にするため、通信には両機の更新が必要。

2026-10-11のユーザー指示が、AGENTS.mdの旧「GP7受信専用」制約に優先する。GP6には出力しない。元のAquaBeacon/P_MAIN_experimentを変更せず、Spresenseロガー更新版をfirmware/spresenseへ複製した。

## 通信

親機GP7/D03双方をLOW出力または高インピーダンスにし、HIGHは能動出力しない。通常のSerial UART/Dallas 1-Wireではなく、200µsタイマー・4サンプル/bitの1250bps・8N2 GPIO UART。128byte CRC32フレームで親機から問い合わせ、Spresenseは10ms以上の切替待ち後に返信する。返信後1秒待って次の定期状態/UTC問い合わせ。応答期限3.2秒、通信鮮度5秒。操作の応答が失われた場合は同一セッション・連番・全ペイロードを最大2回再送する。Spresenseは保存済み応答を再利用し、操作と参考UTC設定を繰り返さない。最終期限切れでは送信中止・線を解放する。新しい連番での操作自動再送はしない。セッション/連番/操作で返信を照合する。直近2セッションの重複要求は応答を再利用し、古い要求は拒否する。

## コマンドと結果

GET /api/status の spresense に状態を表示する。POSTには既存の X-Aqua-Control: 1 とOrigin検査が適用される。

| POST /api/spresense/ 以下 | 意味 |
|---|---|
| stop | 記録停止を予約。キュー排出・同期・クローズ後の last_operation=2 / last_result=0 で完了 |
| start | 新規ファイルで再開。last_operation=3 / last_result=0 で成功 |
| reset | 停止完了確認後に再起動。返信送信が終わってから実行 |
| calibration/start | 地点のgravity / latitudeをPOST本文で指定。記録停止後に6姿勢校正を開始 |
| calibration/status | 進捗、完了姿勢、適用・保存番号・保存/復元エラーを確認 |
| calibration/clear | ソフト補正を無効にした新規レコードをSDへ保存。IMU内部係数は保持 |
| calibration/pose/0〜5 | +X/-X/+Y/-Y/+Z/-Zを上向きにした測定を開始 |
| calibration/save | 全6姿勢の完了後にSDへ保存、読戻し、適用 |
| calibration/end | 測定を中止。未保存候補は適用しない |

HTTP202は親機への受付だけ。command_idに一致したcommand_resultは0完了、1処理中/停止条件不成立、2無効、3未対応、4失敗、5Spresense受付済み、6期限切れ/実行結果不明。STOP/STARTは受付と実処理完了を分ける。タイムアウトは未実行の保証ではない。センサ校正タブに状態・記録停止/再開・停止後再起動を追加した。

既存ログの削除・SDフォーマットはしない。起動時の従来の排他的SD読戻し試験ファイルだけは作成して削除する。

## 参考時刻とログ

親機の既存時刻源（GNSS/端末/RTC）を使用。送信開始時に有効なUTCから、最終byte受信の公称時刻へ1125400µs進めた値を送る。SpresenseはCRCフレームの処理時刻を単調時計のアンカーにし、参考UTCを最大5秒保持する。無効時刻源は解除する。タイマー遅延、受信処理待ち、線の立上り、絶対精度は未測定で、公称補償値は精度保証ではない。

PPS未接続のためPPS同期成立は表示しない。Spresenseのシステム時計とIMU生データは変更しない。Ixxxxx.CSVの単調時計と、Txxxxx.CSVのparent_reference_mono_us / UTC / parent_time_source（0無効、1親機GNSS、2端末、3RTC）から対応を復元できる。センサー内部の19.2MHz生カウンタと、ホスト読出し時刻は別。Spresense内蔵GNSS/D01送信/D02出力はこの版で開始しない。

## Sony公式のIMU校正

単一姿勢の平均をゼロにするだけでは、地球自転も取り除いてしまう。公式手順は以下である。

- 加速度: 実施地点の重力値を設定し、X/Y/Zの正負6姿勢を各10秒静止。振動などで閾値を超えた区間はやり直す。
- ジャイロ: 緯度を設定して地球自転を考慮。Z-UP、USB端子を東へ向け、真方位を±5度以内で合わせて10秒静止。
- 専用ツールはSpresenseに専用アプリを書き込む。対象の既存ユーザー係数を初期化する工程がある。校正後は今回のロガーとformatterを再書込みする必要がある。

公開マニュアルには内部係数形式や全静止閾値がない。SDKのSNIOC_SCALIBはIMUフラッシュへ書くため使用しない。今回の完成版は、既存ドライバ出力を補正するソフトウェア校正であり、Sony専用ツールと同じ内部校正ではない。

## 実装したソフトウェア校正

6姿勢それぞれを明示選択し、2秒整定後10秒静止する。姿勢方向0.995（約5.7度）の判定、連続サンプル間隔3ms以下、センサ時刻欠落なし、10秒間9120点以上、加速度標準偏差0.02m/s²以下・ジャイロ0.001rad/s以下などを条件とする。動き/欠落/非有限値で区間を再取得。これらは本アプリ独自の閾値で、Sonyの非公開精度閾値ではない。セッションは20分で失効。

加速度は対向姿勢の平均と差から3軸オフセットと倍率を算出する。ジャイロは6姿勢の重力方向uと平均角速度wから `u・w = u・bias + Ωsin(latitude)` を最小二乗で解く（Ω=7.292115e-5rad/s）。水平の自転成分を重力方向へ投影しないので、USB端子の方位を推測せず地球自転の鉛直成分を残せる。これはSonyのZ-UP・東向き単姿勢手順とは異なるアルゴリズム。軸間行列や温度依存性の完全補正・精度保証は対象外。

SDのK00001.TMP等を排他作成し、64byte版1・CRC32・同期・読戻しを確認した後、未使用のK00001.CALへ名前変更して適用する。失敗したTMPは残し復元対象から除外。既存ファイルを削除/上書きしない。起動時に有効な最大番号CALを復元。解除はenabled=falseの新規レコードを保存する。保存失敗時は前の適用値を維持する。

Ixxxxx.CSVには従来の生のgx/gy/gz/ax/ay/azを保ち、corrected_gx/gy/gz/ax/ay/azとcalibration_generationを追加（schema=2）。補正はIMU取得後に適用し、センサー内部へ書かない。MainCore/SubCore1の共有バッチABIも版2に変更し、両方の更新が必要。追加列によるSD負荷は実測が必要。

Arduino 3.4.7のmicros()は64bit・32768Hzの電源投入後RTCカウンタを使う。IMU/校正/参考UTCの単調時計にはこれを使い、10ms粒度のCLOCK_MONOTONICは使用しない。受信バイトの区間判定にはISRで保存した受信時刻を使い、処理タスクの10ms/20ms待ちを通信断と誤認しない。

USBからも親機へ `@SPR op gravity latitude pose\n` を送信できる。op2停止、3再開、4安全リセット、5開始、6状態、7解除、8姿勢、9保存、10終了。開始/姿勢の引数は必須。例: `@SPR 5 9.801 35 255`、`@SPR 8 9.801 35 4`。参考UTCは操作要求にも自動添付する。

## ビルドと検証

- Pico: ../P_MAIN_experiment/.venv/Scripts/pio.exe run
- Spresense: powershell -File scripts/build_spresense.ps1（Sony Arduino 3.4.7、MainCoreとSubCore1）
- ホスト: ../P_MAIN_experiment/.venv/Scripts/python.exe scripts/test_host.py
- オフライン画面: python scripts/test_spresense_ui.py

Spresenseビルドではcommon/の正本4ヘッダーをローカルArduinoライブラリへコピーする。親機・Spresense MainCore/SubCore1を更新し、実機の結果を完成版レポートへ記載する。書込み前に親子をUSBシリアル番号と応答で識別し、Spresenseの記録を安全停止する。GPIO波形、IMU/テザー/音響へのタイマー負荷、時刻精度は実測が必要。検証結果は[完成版レポート](../reports/2026-10-11_003_spresense_complete/README.md)参照。Web実機確認はユーザーが行い、PC Wi-Fiは切り替えない。

## 一次資料

- [SonyマルチIMU製品仕様](https://developer.spresense.sony-semicon.com/ja/buy-spresense/spresense-imu-add-on-board): CXD5602PWBIMU1J。公開製品仕様に校正アルゴリズムの詳細はないため、以下の公式専用マニュアルで手順を確認した。
- [Sony拡張ボード回路図](https://github.com/sonydevworld/spresense-hw-design-files/blob/master/CXD5602PWBEXT1/schematics/CXD5602PWBEXT1_schematics.pdf) p.1: D03/PWM3・LSF0108・RN3
- [Sony校正手順・プレート1枚](https://developer.spresense.sony-semicon.com/ja/downloads/imu-calibration-tool/manual-version-2)
- [Sony校正ツール](https://developer.spresense.sony-semicon.com/ja/downloads/imu-calibration-tool)
- [Sony SDKのIMUドライバ](https://github.com/sonydevworld/spresense-nuttx/blob/master/drivers/sensors/cxd5602pwbimu.c): SNIOC_SCALIB / USER_CALIB_FLASH
- [TI LSF0108](https://www.ti.com/product/LSF0108)

オフラインで保存ログを確認する場合は `python scripts/verify_spresense_log.py Ixxxxx.CSV --calibration Kxxxxx.CAL` を使用する。版2ログで補正番号が0以外の場合は対応するCALファイルが必要。CRC/補正値整合の確認であり、精度を認定しない。

## Spresenseの保存診断（2026-10-24）

状態タブとセンサー校正タブにSpresenseの通信、IMUサンプル、欠落・キュー廃棄、SD書込み済み／同期済み行数、SDエラー累計、最後の障害段階・errno、復旧試行／成功回数を表示する。5秒の通信失効後は保存状態を不明とする。SD復旧は「SpresenseのSDを再認識・記録再開」で新規ファイルへ再開する。親子の記録操作とは独立している。

128byteフレームのpayload[88..103]を版マーカー0xD1付きSD診断に使用。従来この領域のジャイロバイアスは親機で未使用。旧ファームの診断は未取得として扱う。USBの状態JSONにもsd_rows / sd_synced_rows / sd_state / sd_failure / sd_detail / sd_attempts / sd_recoveries / imu_gapsを追加した。同期済み行数は両ログのfsync成功時点の累計であり、電源断耐性や欠落なしの保証ではない。

SpresenseはSDの認識、検査ファイル、ログ作成、ヘッダー同期、実書込み、同期、整形処理の失敗を区別する。ヘッダーも同期してから記録を開始。CSV整形の浮動小数点printfを固定小数の直接変換に置換し、列・小数桁・CRCの形式を維持する。共有バッファを2個にして、完了バッファのSD書込み中に別バッファのCSV整形を進める。各バッファはMainCore/SubCore1の片側だけが所有する。USB停止も親機と同じ完了確認経路に統合し、過去のSDエラー累計だけで正常な停止を失敗扱いしない。既存ログは保存し、SDフォーマットを行わない。960Hzの実保存継続・タイムアウトと欠落の実測結果は[検証記録](../reports/2026-10-24_004_spresense_sd_console/README.md)を参照する。実機Web表示はユーザー確認、PC Wi-Fi切替は行わない。

## Spresense SD再発防止（2026-10-24）

IMU取得と同時に1024/6000byteのSD書込みを行うとタイムアウトする条件を実機で再現した。SpresenseのwriteCompleteを、1回最大512byteかつ現在のファイル位置からセクタ境界を越えない書込みへ変更した。CSV形式、取得設定、校正・通信形式は維持し、エラーを握りつぶす再送はしない。今後のSpresense SD書込みもこの境界制約を守る。

対策版はIMU・通信タイマー併用で4MiBの書込み／全量読戻しを3回連続完了し、通常ロガーの300秒観測でもSDエラー・IMUエラー・欠落・キュー廃棄0を確認した。これは当該条件・時間での結果であり、SDK内部の最下層の機序や無期限の安定性までの合格ではない。RTK-GNSS別作業との競合を避け、親機USB・親機ファーム・GNSS設定は操作していない。[原因切り分けと検証記録](../reports/2026-10-24_006_spresense_root_cause/README.md)。

## 2026-10-25 PPS入力・IMU時刻推定

上記のD02未接続・PPS同期未実装は2026-10-11時点の構成。現行追加実装ではJ_SPR1.1のGNSS PPSをSpresense拡張D02で入力し、D03通信でPPSのUTCラベルを受け取る。D02は出力しない。親機側3本の1番PPS・2番通信・3番GNDを設計資料で照合したが、Spresense側の現物終端は未確認。PPS検出とUTC対応の成立を別々に表示する。

IMU CSV/共有バッチABIをschema3へ更新し、全行にサンプル推定単調時刻/PPS対応UTC/時計世代/親機校正セット参照を追加した。従来の参考UTCと生値・補正値は維持する。MainCoreとSubCore1の両方、親機も更新する。PPSはサンプル時刻の換算基準であり、IMUサンプリング回路の位相を駆動するハードウェア同期ではない。固定遅延・精度は未測定。詳細な条件・記録項目・互換性は[親機校正セット](parent_calibration.md)を正とする。

実機追加確認：IMU生タイムスタンプ差分の完全一致判定を、取得の欠落検査と同じ周期窓へ変更。正常な実データの揺れで6姿勢校正が毎回リセットされる問題を防ぐ。時計推定も位相を連続化し、PPS/IMU時刻診断をUSBへ追加した。実機結果は[2026-10-25配備](../reports/2026-10-25_004_pps_deployment/README.md)。
