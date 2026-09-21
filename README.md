# AquaBeacon P-MAIN 実験ファームウェア


## 親子実験のWeb操作画面

**[AquaBeacon 親子実験コントローラー](https://ry32767.github.io/P_MAIN_experiment/main-control/)**

新しい親子統合ファーム用の画面です。測距開始・停止、親子SD記録、BNO50/100Hz切替、受信確認、ログ回収を操作できます。このリポジトリにある従来のSpresense実験ファームとはAPIが異なります。

1. インターネット接続中に公開画面を開き、「画面を保存しました」を確認します。
2. 操作用端末を機体Wi-Fi `AquaBeacon-MAIN` に接続します。
3. 「親機に接続」を押し、対応ブラウザでローカルネットワークへのアクセスを許可します。
4. 接続できないブラウザでは、画面内の「機体内の操作画面を開く」から http://192.168.4.1/ を使用します。

公開画面は端末から親機へ直接通信します。機体がインターネットにつながる構成ではありません。ログをGitHubへ送信しません。Pages用通信許可を追加した親機統合ファームが必要です。Wi-Fiパスワードは公開しません。

P-MAIN（Pico **2 W**）＋P-PWRと、Sony Spresense＋マルチIMU Add-onの取得・記録実験用です。AquaBeaconの基板設計と実機配線メモからピンを確認した独立リポジトリです。

**状態：3構成のビルドとPC上のテストは合格。実機への書き込み、SD実測、UTC絶対精度は未検証です。**

| 機器 | 実装内容 |
|---|---|
| Spresense | 内蔵GNSS 1 Hz、Sony CXD5602PWBIMUを960 Hz設定で取得、SubCore 1でCSV整形・CRC、ハードウェア1PPS出力、D01 UARTでGPS・状態を送信、SDへIMU/GPSを別々にCSV記録 |
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
arduino-cli compile --fqbn SPRESENSE:spresense:spresense:Core=Sub1 --libraries common --build-path build/formatter firmware/spresense/formatter
arduino-cli compile --fqbn SPRESENSE:spresense:spresense --libraries common --build-path build/spresense firmware/spresense/logger
arduino-cli upload --fqbn SPRESENSE:spresense:spresense:Core=Sub1 --port COM番号 --input-dir build/formatter firmware/spresense/formatter
arduino-cli upload --fqbn SPRESENSE:spresense:spresense --port COM番号 --input-dir build/spresense firmware/spresense/logger
```

このPCの専用インストールを使う場合は、Arduino CLIに `--config-file .tools/arduino-cli.yaml` を付けます。CLI実体はArduino IDEに同梱されています。補助スクリプト `scripts/build_spresense.ps1 -Cli <CLIのパス> -Config .tools/arduino-cli.yaml` も使用できます。

Arduino IDEを使う場合は `common/AquaBeacon` をスケッチブックの `libraries/AquaBeacon` にコピーし、`firmware/spresense/logger/logger.ino` を開きます。ボード=Spresense、MainCore、Memory=768KB。先に`formatter/formatter.ino`をSubCore 1として書き込み、その後loggerをMainCoreへ書き込みます。初めて使うボードはSony公式手順に従ってブートローダーを用意してください。

取得タスク（優先度150）とGNSSタスク（120）はMainCoreで動き、SubCore 1が共有バッファのCSV整形・CRCを行います。MainCoreは戻ったバッチをSDへ書き込みます。サブコア応答が2秒途切れた場合は記録失敗として停止します。設定値は`common/AquaBeacon/src/ImuBatch.h`に集約しています。一定周期性はセンサー時刻と受信時刻を分けて実測評価します。

## スマホで使う

1. Picoを起動し、USBモニターへ `h` を送ります。SSID **AquaBeacon-PMAIN** と起動ごとに変わるパスワードを表示します。
2. スマホをそのWi-Fiに接続します。「インターネットなし」でも接続を維持します。
3. ブラウザーで **http://192.168.4.1/** を開きます。
4. 「記録を停止・保存」後、ファイルの「保存」でCSVを取得します。開始すると新しい名前のログを作ります。

固定パスワードにしたい場合は、Git管理されない `firmware/pico/include/secrets.h` に `#define AQ_AP_PASSWORD "8文字以上の任意のパスワード"` を定義してビルドします。32〜63文字を推奨。Wi-Fiは近距離での実験用APで、インターネット経由の遠隔監視機能はありません。

従来のログビューアは、GitHub PagesのHTTPS画面からPicoのHTTP APIを直接呼ぶ構成にはしていません。この従来画面はローカル機体ページへ移動する入口と、ダウンロード済みログを解析する画面です。ネット接続なしでもPico本体が同じ画面を配信します。SpresenseのSD全体をPicoから取得する機能は、現在の片方向配線では実装していません。

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

## 電池残量の表示

親機・子機ともユーザー申告の11.1 V / 2200 mAh / 50C / 24.42 Whを標準3S LiPoとして扱います。既存INA226の電源電圧から、10.5 Vを0%、12.6 Vを100%とした線形の目安を5%刻みで表示します。これは実測容量の割合ではありません。50Cは残量計算には使用しません。10.8 V以下を低電圧表示にする暫定設定で、自動停止はしません。

電圧未取得、周辺電源未検出、6 V未満（USB給電など）、12.9 V超では残量不明とします。子機通信が3秒以上途絶した場合、または親機への状態取得に失敗した場合も残量を消します。電池の放電曲線・温度・負荷補正は未校正で、残り時間や残りmAh、各セル電圧は推定しません。定電圧電源の場合も電圧目安になるため、電池の残量とは解釈できません。

表示方式の参考：[INAVの電圧ベース残量計算](https://github.com/iNavFlight/inav/blob/master/src/main/sensors/battery.c)。閾値は今回の表示用の暫定設定で、当該電池メーカーの保証値ではありません。

## Spresense・GPS時刻同期の状態表示

2026-09-20の通電開始に伴い、親機GP6でSpresense D02の1PPS、GP7でD01のAQB1 UART（115200 bps・受信専用PIO）を受信します。外付けGNSS用GP8/GP9は使用しません。既存Spresenseファームウェアは変更不要です。

WebはSpresense通信、GPS測位成立、測位使用衛星数、1PPS検出と周期、GPS時刻の有効性、親機とUTCの対応、Spresense SD/IMU状態を別々に表示します。現在のAQB1データには可視衛星数・信号強度がないため、未測位時の電波受信有無は「不明」とします。通信途絶時は過去の測位状態を現在値として表示しません。

UTC対応は、1PPSと有効UTCの連続3組、約1秒の周期、UART受信が直前PPSの20〜800 ms後という既存実験の条件を利用します。時刻ラベルが直前PPSに対応すること・絶対時刻精度は未検証です。APIの`spresense.sync_candidate`で条件成立を示し、`utc_candidate_us`は参考値です。従来の`utc_valid=false`は精度未検証として維持します。GPS通信は3秒、同期は1.5秒の期限で失効し、画面も更新停止3秒で不明表示になります。

子機のテザー同期と測距は親機の単調時刻を引き続き使用します。今回UTCを子機へ配信したり、既存SDログの時刻をUTCに変更したりはしません。旧予約ファイルは後述の時刻・位置ログに置き換えました。

## スマホからの位置・時刻共有

1. スマホで[GitHub Pagesの操作画面](https://ry32767.github.io/P_MAIN_experiment/main-control/)を開き、親機へ接続します。ブラウザーが親機への通信を許可できる環境が必要です。
2. 「スマホから位置・時刻を共有」を開き、「時刻共有を開始」または「位置・時刻共有を開始」を押します。位置を使う場合はブラウザーの位置情報許可が必要です。端末の日時は自動設定を使用してください。
3. 画面を開いている間、約30秒ごとに更新します。他の実験操作中・画面が非表示の場合は更新を延期します。共有停止で親機の時刻・位置参照を解除できます。SD保存済みデータは残ります。

位置はこの画面を開いている端末の位置で、親機そのものの位置とは限りません。GPS・Wi-Fi等のどの方式で取得したかはブラウザーから判別できません。位置取得に失敗しても時刻共有は継続します。HTTPの親機内蔵画面は通常位置情報APIを利用できませんが、時刻だけの共有は可能です。HTTPSのGitHub Pagesを利用してください。

時刻には`Date.now()`による端末時計を使用し、位置取得時刻をGPS時刻とは扱いません。往復通信の中点に相当するUTCを親機の単調時刻に対応付けます。往復1秒超や送信中の端末時計変化は拒否。表示する往復時間は通信指標であり、絶対時刻誤差の保証ではありません。時刻は最終プローブから3分、位置は取得から1分で失効します。再起動時は消去されます。

記録用UTCの優先順位はSpresenseのUTC対応候補→有効な端末時計→未同期。Spresense自身の時計や子機の時計を変更せず、子機とのテザー相対同期と測距は従来の単調時刻を維持します。絶対UTC精度は未検証です。

親機SDが正常なら、毎回新規の`P_MAIN_TIME_POSITION_00000.csv`等に1秒ごとに親機単調時刻、参考UTCミリ秒、時刻源、端末時刻の更新経過、通信往復時間、スマホ位置と精度、CRC32を保存します。以前の`P_MAIN_GNSS_RESERVED`の新規生成は廃止し、既存ファイルは保持します。時刻未取得はUTC値0と`time_source=none`、位置失効は有効フラグ0とNaNです。位置は親機へ直接送信し、この画面のコードはGitHub等へ送信しません。

実装根拠：[W3C Geolocation](https://www.w3.org/TR/geolocation/)（位置の取得元、HTTPS・許可、取得時刻の意味）。

## 親機SDの復旧（2026-09-21）

先行検証ではWRITE_FAILED後の再マウントで復旧し、時刻・位置ログとヘルスログ各172行のCRC32が一致しました。根本原因は未特定のため、復旧機能は接触・電源・媒体の問題を解消するものではありません。

親機は保存失敗時、5秒、15秒、60秒の待機を挟んで最大3回、自動再認識します。毎回512バイトの新規ファイル読戻しを検証し、新しい番号のログで保存を再開します。旧ログの削除・上書き・フォーマットは行いません。復旧後30秒連続で正常なら試行上限をリセットし、短時間で繰り返す故障では無限に再試行しません。失敗から復旧までの記録は欠落し、後から埋め戻しません。

Webの「親機SDの復旧」で、子機なしでも親機だけの復旧・記録再開と停止ができます。上限到達後も復旧ボタンで再試行可能です。正常保存中の復旧ボタンは新しいファイルを作らず、そのまま継続します。「親機の記録・自動復旧を停止」や従来の記録停止は自動復旧も無効にします。USBのs/qも同様です。安全停止時にSDエラーが出た場合でも自動再開は抑止されますが、保存成功を意味しません。

`sd_recovery`には有効/待機/上限到達、連続試行回数、自動復旧成功累計、最後の失敗理由を表示します。既存のSDエラー累計も保持し、復旧成功で過去のエラーを隠しません。起動時は自動記録・自動復旧が有効になります。今回の改良は親機限定で、子機は接続・書込みしていません。
## Spresense GNSS受信改善（2026-09-21）

GPS+GLONASS+QZSS L1C/Aを使用し、初回測位を優先してからIMU 960Hzを開始します。IMU開始待ちは最大120秒で、測位後のFix喪失ではIMUを止めません。電源投入直後はIMUログが空の時間があります。実機ではGNSS開始から約34秒で3D測位し、IMU・SD稼働中も継続しました。[検証レポート016](reports/2026-09-21_016_gnss_multisystem/README.md)。親機UTC同期精度とは別の検証です。
