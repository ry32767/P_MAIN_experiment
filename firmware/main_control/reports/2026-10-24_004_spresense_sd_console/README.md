# SpresenseのSD保存・コンソール状態表示

## 前提

- 実施日: 2026-10-24 JST。親機Pico 2 W / Arduino-Pico、Spresense Sony Arduino 3.4.7。
- 親機COM6 / USBシリアルA444A41EE9334705 / role=P_MAIN、Spresense COM7 / USBシリアル10878C89E6DBED1197F5F12D62C613AC / IMU_LOG・FORMATTER応答で識別してから安全停止・書込み。
- include/pins.hと隣接AquaBeaconのbuild_parent_main.py / build_child_main.pyを参照。GPIO割当・配線・通信方式は変更していない。実配線の再目視は未実施。現行配線の前提はGP7–D03・GP6親機GNSS PPS。D02未接続の仕様を維持。
- SDの媒体型番・表示容量・給電品質は未確認。statfsの総容量は532676608 byte。カード情報はユーザーへ照会。USB接続開始時に起動ログと新規ファイル作成を観測したため、今回の認識成功を恒久的な修復とは扱わない。
- PC Wi-Fiの切替、実機Web操作、フォーマット、既存ログ削除は実施していない。元のAquaBeacon / P_MAIN_experimentは変更していない。公開画面のみ専用P_MAIN_pages_publish checkoutで更新。親機温度・水圧・LCD、送信・測距動作は検証対象外。

## 実施

- 状態タブにSpresense・SD保存欄を常設。センサー校正タブの同欄も拡充し、取得・欠落、書込み済み／同期済み行数、SD障害段階・errno、自動復旧の状態・回数を表示。通信失効後は保存状態不明へ移行する。
- 固定128byteフレームの未使用だったpayload[88..103]に版マーカー付き診断を追加。旧ファームでは診断未取得扱い。親機USBのhと/api/statusにも同じ診断を出す。
- SD認識／検査作成／検査I/O／ログ作成／ヘッダー／書込み／同期／整形の失敗を区別。ヘッダーも同期してから記録を有効化する。
- CSVの浮動小数printfを固定小数変換に置換。2個の共有バッファでCSV整形とSD書込みを並行させる。列・小数桁・CRCは維持。最高クロックを明示し、実測した変更前もmode=0（156MHz）だったのでクロック変更による改善とは評価しない。
- USB qを親機と同じ停止完了経路に統合。障害で既にファイルが閉じている場合も完了応答。過去のSDエラー累計だけで、その後の正常な停止を失敗にしない。
- 親機、Spresense MainCore/SubCore1を書込み。停止・実ログ回収・新規ファイルでの再開を実施。最後に親機経由の状態を保存。

## 実測

| 項目 | 結果 |
|---|---|
| SD認識・検査 | 今回は起動時SD_READBACK_OK。過去の未認識の根本原因は未特定 |
| 修正前の継続保存 | errno=110の書込みタイムアウトを複数回観測。IMU取得に対して保存が遅くキュー廃棄も発生 |
| 最終版の継続保存 | pipeline_45sで書込みタイムアウトが再発。自動復旧と新規ファイルへの再開は観測。安定記録・無欠落は不合格 |
| 実CSV読戻し | 新規の短い記録を安全停止後、USBで260010byteの完全なEXPORT_END付き回収。1428データ行のCRC・順序・有限値・無補正時の生/補正値一致を検証成功 |
| 最終状態 | sd_state=RECOVERING / recording=False / sd_ok=False / sd_rows=17472 / synced_rows=16959 / SD障害=WRITE errno=110 / sd_errors=7。最後は書込み障害からの自動復旧待ち |
| IMU／通信 | 最終照会でIMU取得有効・link_fresh=true。累計の通信期限切れ・framingエラーも残るため通信無欠落とはしない |
| 画面公開 | commit a4b880a29b936dd4f0d9acc251e85e7bb3dbb7da。[Publish monitor](https://github.com/ry32767/P_MAIN_experiment/actions/runs/40099148048)成功。公開HTML59230byte・新しい状態欄と同期行数の配信を確認 |

同期済み行数は両ファイルのfsync成功時点の累計。今回回収した短いログの整合性成功と、長時間の保存安定性は別の判定とする。媒体交換・接触・給電等の切り分けが残っており、ソフトウェアだけで解消したとは書かない。タイムアウトが媒体起因と確定したわけでもない。

## ソフトウェア検証

- 指定環境のPico parent / child100ビルド成功。Spresense formatter / loggerビルド成功（最終logger 306248byte）。
- scripts/test_host.pyは指定venv Pythonで成功。既定pythonにはziglangが無かったため指定環境へ切替えた。追加検証は版付きSD診断の往復・旧版/不正値拒否、固定小数変換20000値のprintf一致・負のゼロ・容量不足・非有限値拒否。既存のCSV18列・校正・CRC試験も成功。
- test_spresense_ui.py / test_console_ui.py / 公開専用Pages模擬試験成功。通信失効時の保存状態解除、SD異常とerrno、同期行数、既存操作ガードを確認。
- 書込み済みビルドのSHA256は生ログのdeployed_sha256.jsonへ保存。

## 未検証・必要な切り分け

カード差し直し・別のmicroSDカードでの比較、電源・SD信号の実測、960Hz無欠落の継続保存、再起動をまたぐ校正値保存、スマホからの実機Web表示・操作、GPIO波形、絶対時刻・距離・IMU精度。カード交換は停止完了後に電源を切って行う。既存ログを保持する。

## 根拠

[生ログ一式](../raw/2026-10-24_004_spresense/): spresense_before.txt / sd_probe.txt（修正前）、pipeline_45s_usb.txt / pipeline_45s.json（最終継続試験）、final_imu.csv / csv_verification.txt（実読戻し）、final_parent.json（最終照会）、build_pico.txt / build_spresense.txt / host.txt / ui.txt / console.txt / pages_test.txt / upload_* / deployed_sha256.json。
