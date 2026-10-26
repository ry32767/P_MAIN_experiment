# 親機コンソール接続確認（2026-10-24）

## 前提

- ユーザー申告: Spresenseと親機メインを接続。送信基板には何も接続していない。
- 親機USB: COM6、シリアル番号 A444A41EE9334705。Spresense USB: COM7、シリアル番号 10878C89E6DBED1197F5F12D62C613AC。
- 温度・水圧・LCDは対象外。GP6/GP7の配線・受信設定は変更していない。

## 実施

- USB列挙、親機COM6を115200bpsで開き h/w の状態照会のみ実施。終了時にポートを解放。
- PCのWi-Fi接続状態を参照。Wi-Fi切替、実機Webアクセス、送信開始、ファーム書込み、再起動、SD操作、ログ削除・フォーマットは行っていない。

## 実測

- 親機のrole=P_MAINを確認。約8秒の取得で10件の状態JSONを取得。USBコンソールへの接続・応答は成功。
- 親機Wi-Fi情報はSSID AquaBeacon-MAIN、IP 192.168.4.1を応答。wの応答は設定情報であり、無線経由の実際の到達性を保証しない。
- PCのWi-Fiは親機APとは別のネットワーク。PCからWebが開かない原因候補。実際のブラウザーエラーは未確認。
- 親機SD: sd_reason=NONE、保存行数/同期済み行数685→693。今回の観測中は記録継続。過去のREADBACK_FAILEDからの自動復旧1回・SDエラー累計1回を保持しており、長時間安定性の合格とはしない。
- Spresense: link_fresh=true、受信214→216packet、IMUサンプル677266→683604、imu_ok=true、CRC/framing/collision/overflow各0。timeout累計1は観測中増加なし。
- Spresense: sd_ok=false、recording=false、sd_errors=6。SD未認識の原因は未特定。
- 測距送信はranging=falseを維持。子機の通信応答は0。
- GNSS通信はfresh、測位/有効時刻/PPSは未取得。絶対時刻・距離精度の合格判定は行っていない。

## 未検証

- ユーザー回答により、接続できない対象はブラウザーの親機Webコンソールと確認。ブラウザーを開いている端末とエラー内容は未確認。
- スマホ等から親機APへ接続した実機Web表示・操作、Spresense SDの挿入/媒体状態・保存、送信・音響動作、実配線、長時間安定性。

## 生ログ

- [親機USB応答（Wi-Fi接続情報を含む）](../raw/2026-10-24_001_console/parent_status.txt)
- [状態JSON](../raw/2026-10-24_001_console/summary.json)
