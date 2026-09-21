# AquaBeacon MAIN firmware
日本語で回答。Pico 2 W / Arduino-Pico / PlatformIO。
- ピンの正は include/pins.h。隣接AquaBeaconのbuild_child_main.py/build_parent_main.pyと実配線で照合。元リポジトリは変更しない。
- Spresense GP6はPPS入力、GP7はPIO UART受信専用。ユーザーの通電開始指示により受信を有効化。親機温度/水圧/LCDは検証対象外。
- 書込みはUSBシリアル番号と応答の両方で親子を識別してから。SDフォーマットや既存ログ削除禁止。
- 検証は reports/日付_番号_目的/README.md に前提・実施・実測・未検証を記載。生ログ/無線秘密はreports/raw以下。
- 仕様変更時はdocs/spec.mdとREADMEを同時更新。docs増減時はdocs/README.mdも更新。
- ビルド: pio run
- ホストテスト: python scripts/test_host.py
- 未測定の時刻/距離精度を合格と書かない。

- ユーザー指示: PCのWi-Fiを切り替えない。切替によりCodex通信が途絶するため、実機Web画面の確認はユーザーが行う。Wi-Fi切替を含むtest_web_device.py/test_web_controls.py等は実行しない。
