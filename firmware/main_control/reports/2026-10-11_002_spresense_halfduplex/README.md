# Spresense 1線半二重通信の実装・ビルド検証

## 前提

- ユーザー申告: Pico GP7–Spresense拡張D03、共通GND、JP1=3.3V、追加4.7kΩなし、D02未接続。
- GP6は既存の親機GNSS PPS入力を維持。今回の通信処理からGP6へ出力しない。
- include/pins.hと隣接AquaBeacon/pcb/build_parent_main.py / build_child_main.pyを照合。親機J_SPR1.2は100Ω直列の旧EVENT配線。子機のGP6/7は別用途であり、親機通信コードを子機で開始しない。
- 最新ユーザー指示に従いGP7をLOW出力/解放による半二重に変更。旧AGENTS.mdの受信専用記述は変更していない。
- 元AquaBeaconおよびP_MAIN_experimentのソースは変更せず、Spresenseロガーを本作業場所へ複製。ここはGitメタデータを持たないためコミット識別子なし。
- 公式拡張ボード回路図でD03のRN3 1kΩプルアップを確認。実物の電圧・抵抗値は未測定。

## 実施

- 2500bps・8N2・100µsタイマーのGPIO通信を両機へ実装。HIGHは能動出力せず、LOW出力または高インピーダンス。
- 96byte CRC32要求/応答、セッション/連番照合、重複抑止、応答期限、通信鮮度を実装。
- 親機から参考UTCを送り、Spresenseで単調時計との対応をTxxxxx.CSVへ保存。PPS同期成立や絶対精度の保証は表示しない。
- 状態取得、記録停止/新規ファイルで再開、停止完了後のリセットを実装。受付と完了を分け、期限切れは実行結果不明と表示。
- Webコンソールへ状態・操作を追加。実機Wi-Fi画面は検証せず、PC Wi-Fi変更なし。
- Sony公式製品仕様・校正マニュアル・SDKドライバを調査。加速度6姿勢、重力値、ジャイロ緯度/東向きの手順を文書化。係数形式などの確認が残るため校正開始/消去はUNSUPPORTED。既存IMU校正値へ書き込まない。
- README.md / docs/spec.md / docs/README.mdと詳細仕様を更新。

## ビルド・模擬試験結果

| 対象 | 結果 | 根拠 |
|---|---|---|
| Pico parent / child100 | 両構成ビルド成功 | [生ログ](../raw/spresense_halfduplex_pico_build.txt) |
| Spresense MainCore logger | ビルド成功、256920byte | [生ログ](../raw/spresense_halfduplex_build.txt) |
| Spresense SubCore1 formatter | ビルド成功、74860byte | 同上 |
| scripts/test_host.py | 成功 | [生ログ](../raw/spresense_halfduplex_host.txt) |
| scripts/test_console_ui.py | 成功 | [生ログ](../raw/spresense_halfduplex_console.txt) |
| scripts/test_spresense_ui.py | 成功 | [生ログ](../raw/spresense_halfduplex_ui.txt) |

ホスト試験はCRC破損/再同期、時刻期限、64bit単調時計、重複要求、4種類のサンプル位相での全フレーム往復、受信あふれ、LOW固定時の送信中止を含む。画面試験は古い状態での操作禁止、停止完了前のリセット禁止、受付/完了/期限切れ表示を確認。これらはソフトウェア上の模擬試験であり実測ではない。

## 実測・未検証

実機測定は今回実施していない。USBへの書込み、SD既存ログの削除/フォーマットも行っていない。

未検証: 配線の電圧/立上り/LOWレベル、タイマー遅延、実機の通信信頼性、IMU960Hz取得・SD保存・テザー/音響への負荷、停止/再開/リセットの実動作、UTCの絶対精度。親機温度/水圧/LCDは対象外。校正の完全実装と精度検証も残作業。

書込み時はUSBシリアル番号と応答の両方で親子を識別する。Spresenseは既存記録の安全停止を確認してから更新する。MainCore loggerとSubCore1 formatterの両方が必要。

詳細・一次資料: [通信仕様と校正手順](../../docs/spresense_halfduplex.md)。
