# テザー受信開始エッジによる片道音響測距

## 前提

- 依頼: 親機音響送信と同時にテザー信号を送り、子機のテザー受信→音響受信の時間差で片道ToFを計算する。追加指示: 近接時も距離を表示する。
- 既存RS485 DATA線を使用。専用トリガ線は隣接AquaBeaconの基板設計に存在しない。
- include/pins.hと隣接build_child_main.py/build_parent_main.pyのGP0/GP1、子機GP2、親機GP16を照合。元リポジトリは変更していない。
- 実配線は既存申告を前提。今回の再確認・通電・USB書込み・Web実機操作は実施していない。親機温度・水圧・LCDは検証対象外。

## 実施

- SCHEDULEは受信待機の準備に使用し、親子offsetによる音響窓の開始を廃止。測距は同期成立を要求しない。
- 親機の同一アラーム処理内でUART開始マーカー0x00と有限PIOバーストを起動。UART/DMA占有時は発射せず、取得異常へ遷移する。
- 子機GP1の立下りを割込みで記録し、マーカーのバイト受信確認後に基準として採用。マーカー確認前に発生した音響エッジもキューから評価する。
- 子機内の音響エッジとテザーエッジの差をToFとし、片道距離を計算。200 µs未満も近接・未校正距離を表示する。
- 欠落・負ToF・窓外・エッジ溢れは距離にしない。親機はRESULT期限切れや発射なしも取得異常とする。
- 通信version=2、旧version=1拒否。CSVのtx_usは子機のテザー受信エッジへ意味を変更。親子両方の更新が必要。
- README.mdとdocs/spec.mdを同時更新。docsのファイル増減はなし。

## 検証結果・実測

- 指定のPlatformIOビルド: parent / child100 / child50成功。最終の親機共有フラグ修正後、既定parent / child100を再ビルドして成功（child50の子機コードへの変更なし）。
- 通常の `python scripts/test_host.py` はziglang不足で実行不可。既存の `../P_MAIN_experiment/.venv/Scripts/python.exe scripts/test_host.py` では全項目成功。
- 追加ホスト検証: 子機内ToF、近接・ゼロ、負値、窓境界、6 µs候補幅、エッジ溢れフラグ、基準信号欠落、32 bit境界越えの64 bit時刻、旧通信バージョン拒否。
- `python scripts/test_ranging_ui.py`: JavaScript構文、相対同期未成立の近接距離、ゼロ距離、取得異常、停止表示のオフライン検証成功。実機Webを開くテストではない。
- 実機の時刻・波形・距離の実測はなし。精度合格とは記載しない。

生出力: [ホストテスト](../raw/2026-10-09_host_test.txt)、[Web表示テスト](../raw/2026-10-09_ranging_ui.txt)、[3構成ビルド](../raw/2026-10-09_ranging_build.txt)、[最終既定ビルド](../raw/2026-10-09_ranging_final_build.txt)。

実装時に参照した公式API: [Pico SDK GPIO/UART/PIO](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html)。ローカルArduino-PicoのattachInterrupt実装でUARTピンの機能を変更しないことも確認した。

## 未検証・次の実機確認

- GP16音響送信開始とGP0テザーマーカー開始のずれ、GP1受信エッジの割込み遅延、RS485の変換・ケーブル伝搬・TX/RX回路遅延。完全同時性は未保証。
- 音波がテザー基準より先に到達する極近接条件、マーカーのノイズ誤検出、比較器の電気的回り込み、反射。
- 両機書込み後の1 Hz測距、近接での参考距離表示、既知距離での校正、テザー断線／音響入力なし／停止時の挙動。
- ユーザーが実機Webを確認する。PCのWi-Fi切替、test_web_device.py/test_web_controls.pyの実行、SDフォーマット、既存ログ削除は行っていない。
