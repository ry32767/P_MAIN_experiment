# AquaBeacon P-MAIN 実験ファームウェア

P-MAIN（Pico **2 W**）＋P-PWRと、Sony Spresense＋マルチIMU Add-onの取得・記録実験用です。AquaBeaconの基板設計と実機配線メモからピンを確認した独立リポジトリです。

**状態：3構成のビルドとPC上のテストは合格。実機への書き込み、SD実測、UTC絶対精度は未検証です。**

| 機器 | 実装内容 |
|---|---|
| Spresense | 内蔵GNSS 1 Hz、Sony CXD5602PWBIMUを120 Hzで取得、ハードウェア1PPS出力、D01 UARTでGPS・状態を送信、SDへIMU/GPSを別々にCSV記録 |
| P-MAIN | GP7のPIO UART受信、GP6のPPS割り込み、UTC対応候補の検査、10 HzのCSV記録、電源基板INA226の電圧・電流監視 |
| 両SD | 512バイトの書き込み→保存確定→閉じる→開き直して読み戻し、既存ログを上書きしない採番、データ行CRC32、保存エラーの記録 |
| スマホ | PicoのWi-Fi APに接続し状態表示、記録停止・再開、P-MAINの確定済みログを取得 |
| GitHub Pages | [ログビューア](https://ry32767.github.io/P_MAIN_experiment/)と機体ページへの入口。CSVは端末内で解析 |

RX基板、水温・水圧センサー、テザー、LCDは無効です。今回の対象に送信実験は含めず、**TX_PWM GP16はLow固定**です。P-PWRには別のCPUはなく、PicoからINA226を読み取ります。電源スイッチ・レギュレーターをソフトウェアで制御する機能はありません。

## 最初に確認する配線

**Spresense拡張ボードCXD5602PWBEXT1はJP1=3.3V、JP10 pin1–2開放。** Spresenseメインボードの1.8V端子には直接接続しません。

P-MAINを部品面から見てJ_SPR1の左から：

| J_SPR1 | 接続元 | Pico側 | 信号方向 |
|---|---|---|---|
| 1 | Spresense拡張D02 | GP6 / 物理9 | Spresense → Pico / GNSS 1PPS |
| 2 | Spresense拡張D01 | GP7 / 物理10 | Spresense → Pico / UART 115200、8N1 |
| 3 | GND | GND | 共通GND |

**旧ファームのEVENT_MARK出力は使えません。GP7を出力にしないでください。** 外付けGNSSのJ_GNSS1はPPSネットが競合するため未接続にします。SpresenseはUSB等から別給電します。

P-PWR→P-MAINのJ_PWR1は、1/2=5V、3/4=GND、5/6=3.3Vです。PicoのUSBだけではSD・INA226側の3.3Vレールが給電されません。起動時はボタンを押さず、ADCラダーで3.3Vの存在を確認させてください。詳細は[配線の根拠](docs/HARDWARE.md)。

## Pico：PlatformIO

VS Code + PlatformIOでこのフォルダを開きます。

```powershell
pio run -e pico2w             # 無線あり
pio run -e pico2w_logger      # 無線なし
pio device list              # ポートを確認
pio run -e pico2w -t upload --upload-port COM番号
pio device monitor --port COM番号 --baud 115200
```

UF2は `.pio/build/pico2w/firmware.uf2` に生成されます。BOOTSELで接続した対象のPico 2 WへUF2をコピーする方法も使えます。既存の子機や別のPicoに誤って書き込まないでください。

この作業環境には `.venv` と `.tools/platformio` を作成済みです。CLIは次の方法でも使えます。

```powershell
$env:PLATFORMIO_CORE_DIR = "$PWD/.tools/platformio"
.venv/Scripts/python.exe -m platformio run -e pico2w
```

新しいPCでは `python -m venv .venv`、`.venv/Scripts/python.exe -m pip install platformio==6.2.0` で準備できます。Windowsで依存ライブラリのパス長エラーになる場合は、このターミナルだけ `GIT_CONFIG_COUNT=1`、`GIT_CONFIG_KEY_0=core.longpaths`、`GIT_CONFIG_VALUE_0=true` を設定して再実行します。

## Spresense：Sony公式Arduinoコア

Sony公式コア**3.4.7**でコンパイル確認済みです。マルチIMUの公式ドライバーを使用するため、Spresense側はArduino CLIを採用しました。Pico側と通信定義を共有します。

```powershell
arduino-cli core update-index --additional-urls https://github.com/sonydevworld/spresense-arduino-compatible/releases/download/generic/package_spresense_index.json
arduino-cli core install SPRESENSE:spresense@3.4.7 --additional-urls https://github.com/sonydevworld/spresense-arduino-compatible/releases/download/generic/package_spresense_index.json
arduino-cli compile --fqbn SPRESENSE:spresense:spresense --libraries common --build-path build/spresense firmware/spresense/logger
arduino-cli upload --fqbn SPRESENSE:spresense:spresense --port COM番号 --input-dir build/spresense firmware/spresense/logger
```

このPCの専用インストールを使う場合は、Arduino CLIに `--config-file .tools/arduino-cli.yaml` を付けます。CLI実体はArduino IDEに同梱されています。補助スクリプト `scripts/build_spresense.ps1 -Cli <CLIのパス> -Config .tools/arduino-cli.yaml` も使用できます。

Arduino IDEを使う場合は `common/AquaBeacon` をスケッチブックの `libraries/AquaBeacon` にコピーし、`firmware/spresense/logger/logger.ino` を開きます。ボード=Spresense、MainCore、Memory=768KB。初めて使うボードはSony公式手順に従ってブートローダーを用意してください。

## スマホで使う

1. Picoを起動し、USBモニターへ `h` を送ります。SSID **AquaBeacon-PMAIN** と起動ごとに変わるパスワードを表示します。
2. スマホをそのWi-Fiに接続します。「インターネットなし」でも接続を維持します。
3. ブラウザーで **http://192.168.4.1/** を開きます。
4. 「記録を停止・保存」後、ファイルの「保存」でCSVを取得します。開始すると新しい名前のログを作ります。

固定パスワードにしたい場合は、Git管理されない `firmware/pico/include/secrets.h` に `#define AQ_AP_PASSWORD "8文字以上の任意のパスワード"` を定義してビルドします。32〜63文字を推奨。Wi-Fiは近距離での実験用APで、インターネット経由の遠隔監視機能はありません。

GitHub PagesのHTTPS画面からPicoのHTTP APIを直接呼ぶ構成にはしていません。Pagesはローカル機体ページへ移動する入口と、ダウンロード済みログを解析する画面です。ネット接続なしでもPico本体が同じ画面を配信します。SpresenseのSD全体をPicoから取得する機能は、現在の片方向配線では実装していません。

## 保存・検証

| ボード | ファイル | 内容 |
|---|---|---|
| Pico | `P00000.CSV`〜 | 時刻対応候補、GPS、INA226、各エラー数、メモリ、行CRC32 |
| Spresense | `I00000.CSV`〜 | IMU連番、読み出し時刻、センサー生タイムスタンプ、温度・3軸角速度・3軸加速度、行CRC32 |
| Spresense | `G00000.CSV`〜 | GPSのUTC・座標・状態、IMU/SD統計、行CRC32 |

両方とも概ね1秒ごとに保存確定します。電源断では直近の未確定分を失う可能性があるため、終了時はPicoをWeb画面またはUSBの `q` で停止、SpresenseにもUSBの `q` を送り、`STOPPED_REMOVE_SD` を確認します。Spresenseの停止後の再開は再起動です。Picoは `r` で再開、`s` で再マウント・検証・新規記録、`h` で状態表示です。SDの安全な抜き差しは停止後に行い、挿し直したら再マウントします。

Spresenseは停止完了後にUSBへ `i` を送るとその起動セッションのIMU CSV、`g` でGNSS CSVを出力します。`# EXPORT_BEGIN` と `# EXPORT_END` の間を保存し、`verify_logs.py`で検証してください。記録中の回収は拒否します。USBの`gps_seq/fix/satellites/flags`は記録停止後も更新されます。`visible/max_signal`は受信衛星情報、`tx_bytes/tx_errors`はUART書込APIの受付状況です（端子上の波形を保証する値ではありません）。親機の`h`には受信バイト数・CRCエラー・入力レベルも表示します。

PicoのUSB `i` は記録停止中だけI²C機器の応答とRTC候補のレジスターを読み取ります。アドレスだけで機種は確定しません。SD初期化比較用`pico2w_sd100`は診断専用で、通常構成は`pico2w`です。

```powershell
python scripts/verify_logs.py P00000.CSV I00000.CSV G00000.CSV --output result.json
python scripts/verify_logs.py P00001.CSV --require-sync --output outdoor-result.json
```

後者は屋外で測位を待ち、新しいログを始めてから使用します。CRC、連番、単調時刻、保存・受信・IMU欠落カウンターを確認し、同期候補95%以上を要求します。SHA-256はPCとスマホから取得した同一ファイルの比較にも使えます。**これだけでUTCの絶対精度は証明できません。**

時刻の扱いと制約は[同期方式](docs/TIMING.md)、実機で実施する合否基準は[実験手順](docs/EXPERIMENT.md)、今回確認した結果は[検証記録](docs/VALIDATION.md)にあります。

## 開発とGitHub

`common/AquaBeacon` が通信・時刻判定、`firmware/pico` がPlatformIO、`firmware/spresense` がSony、`web` が画面です。GitHub Actionsで両ファームとテストをビルドします。全ソースを公開リポジトリで管理し、Pagesには `web` だけを配信します。生ログ・認証情報・開発環境はコミットしません。

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Icommon/AquaBeacon/src tests/test_core.cpp -o test_core
./test_core
python -m unittest discover -s tests -v
```

Windowsの今回の環境では `.venv/Scripts/python.exe -m ziglang c++` をg++の代わりに使用しました。

## 実験・検証レポート

実験・検証ごとに `reports/YYYY-MM-DD_NNN_識別名/` を作成し、ピン配置・接続機器・前提条件・実施内容・結果を簡潔に記録します。グラフや図、公開可能な根拠も同じフォルダに保存します。同じ目的の途中ビルド・再テストは1レポートにまとめます。

- [今回の動作確認レポート](reports/2026-09-20_001_initial_validation/README.md)
- [レポート一覧](reports/README.md)
- [テンプレート](docs/REPORT_TEMPLATE.md)
