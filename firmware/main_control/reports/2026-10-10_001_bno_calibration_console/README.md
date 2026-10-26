# BNO085校正操作・親機コンソールのタブ刷新

## 前提

- 本リポジトリの親機内蔵Webと子機BNO085を対象とする。Pico 2 W / Arduino-Pico / PlatformIO。
- BNOは子機SPI0のGP16〜21。include/pins.hと隣接AquaBeacon/pcb/build_child_main.pyのPICO1ネット定義を照合。ピン変更なし。今回の実配線の目視・導通確認は未実施。
- GP6 PPS、GP7受信専用PIO、親機温度/水圧/LCDは変更・検証対象外。
- PCのWi-Fi切替、実機USBアクセス、書込み、SDフォーマット、既存ログ削除、公開Pages更新は行わない。
- 根拠：[CEVA BNO08X Sensor Calibration Procedure 1.6](https://www.ceva-ip.com/wp-content/uploads/BNO080-BNO085-Sesnor-Calibration-Procedure.pdf)、同梱SH2 APIと応答処理。

## 実施

- CONTROL 13〜16で校正開始／状態／保存／終了を追加。core1でBNO操作、core0はID付きキューで結果を待つ。テザー再送は既存キャッシュで二重実行を抑止。
- 開始時は測距停止→子機記録停止完了→校正。校正中の磁気は50Hz、加速度+磁気の動的校正、ジャイロは静置校正。状態0〜3を最新性付きで表示。
- 3センサーの状態2以上・500ms未満の更新を5秒継続した場合だけDCD保存。保存成功応答後の二重保存を拒否し、失敗時は再試行可能。終了は元マスクと磁気20Hzへ復帰。復帰失敗時は校正中フラグを保持して再試行可能。10分で自動終了を試行。
- get/setCalConfig、saveDcdNowに750msの応答期限。同期SH2処理中はcore1のテザー処理に空白が生じるため測距を停止する。core0のHTTP・SD処理は待ちループで止めない。
- 5タブ化、全タブ共通の測距停止と操作結果、矢印/Home/End、スマホ折り返し。既存操作とIDを保持。CSS変数・web/DESIGN.md、README・仕様・索引を更新。

## 実測・検証結果

- `../P_MAIN_experiment/.venv/Scripts/pio.exe run -e parent -e child50 -e child100`：3構成とも成功。
- `../P_MAIN_experiment/.venv/Scripts/python.exe scripts/test_host.py`：既存プロトコル・時刻・SD復旧・バイナリ・連番と校正テスト成功。既定pythonにはziglangがなかったため既存PlatformIO環境のPythonを使用。
- 実際のcalibrationTick/endCalibrationを抽出して偽SH2と接続：BNOなし、開始失敗、状態不足、保存失敗、重複開始/保存、終了復帰失敗と再試行、10分期限、元マスク・磁気周期復帰を確認。ホストの模擬応答であり、実機の保存確認ではない。
- `python scripts/test_console_ui.py`：5組のタブ、単一表示パネル、ID一意、入力ラベル、構文、キー操作、停止失敗時の開始抑止、通信失効表示が成功。
- 本文コントラスト比：ink/panel 12.30、ink/paper 10.95、muted/panel 5.66、muted/paper 5.04、accent/panel 4.90、danger/panel 6.70、good/panel 5.67。すべて4.5以上。
- `python scripts/test_ranging_ui.py`：既存の近接/ゼロ距離・異常・停止表示と周波数設定が成功。端末時刻の中点・座標変換の既存純粋ロジックも確認。
- localhostで1280×900と375×812の表示を確認。375指定ではスクロールバーを除く幅360px、scrollWidthも360pxで横はみ出しなし。実験ボタンの高さ44〜60px。ローカルプレビューは実機未接続のため通信途絶表示。
- 生ログ：[ビルド](../raw/2026-10-10_build.log)、[ホスト](../raw/2026-10-10_host.log)、[UI](../raw/2026-10-10_console.log)、[測距UI](../raw/2026-10-10_ranging_ui.log)、[BNO模擬応答](../raw/2026-10-10_bno_control.log)。

![PC幅のローカル校正画面](../raw/2026-10-10_console_desktop.jpg)

## 未検証・実機での確認手順

1. 親子のUSBシリアル番号と役割応答を両方確認してから両方のファームウェアを更新する。今回は未書込み。
2. ユーザーがスマホ等で内蔵画面の「センサー校正」を開き、開始後の測距停止・子機記録停止・校正中表示を確認する。
3. 画面の動作手順を実施。全センサーが中以上で5秒続いた後に保存し、保存応答を確認して終了。実験タブから記録再開。
4. 再起動/電源断後のDCD保持と通常レポート復帰を実機で確認する。

BNO実機の進捗・保存応答・保持、SPI通信空白の影響、50/100Hz実データ、実機スマホ表示は未検証。終了はRAM補正の巻戻しではなく、既存の自動DCD保存設定も変えない。中断が「既存校正を全く変えない」とは保証しない。

角度・磁北・距離・絶対時刻の精度は未測定で、合格と扱わない。Game Rotation Vectorの座標系は変更しない。公開GitHub Pages画面は未更新で、隣接のソースリポジトリは変更していない。
