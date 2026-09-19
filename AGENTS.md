# P_MAIN_experiment

ユーザーには日本語で回答する。PicoはPlatformIO、SpresenseはSony公式Arduinoコア3.4.7。

- ピン変更は `docs/HARDWARE.md` と隣接AquaBeaconの設計・実配線で確認する。元のAquaBeaconを変更しない。
- GP7はSpresenseからのUART受信専用。旧EVENT_MARK出力コードを流用しない。
- GP16は本取得実験ではLow固定。TX送信は新たな実験指示があるまで追加しない。
- 生ログ・認証情報・Wi-Fiパスワードをコミットしない。
- `sync=1` は対応条件成立を表し、UTC精度の実測合格を意味しない。未検証事項を合格と記載しない。
- 変更に応じてC++共通テスト、Pythonログ検証テスト、両Pico構成、Spresenseビルドを行う。
- 実機書き込み前は対象COMポートと親機/子機の識別を確認する。自動的に最初のPicoへ書き込まない。
- Web原本は `web/index.html`。生成される `web_page.h` を直接編集しない。
