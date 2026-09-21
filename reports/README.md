# 実験・検証レポート

目的ごとに1フォルダ・1レポートを作成します。同じ検証の途中のビルドや再テストは、そのレポートにまとめます。

| 日付 | レポート | 判定 |
|---|---|---|
| 2026-09-19〜20 | [001 初期ファームウェアの動作確認](2026-09-20_001_initial_validation/README.md) | ビルド・PCテスト合格、実機未検証 |
| 2026-09-20 | [002 実機検証：接続確認](2026-09-20_002_hardware_preflight/README.md) | COM6検出・受信0バイト、Spresense接続確認待ち |
| 2026-09-20 | [003 親機への書き込み・起動・SD確認](2026-09-20_003_parent_bringup/README.md) | 書込・USB応答成功、SDマウント失敗 |
| 2026-09-20 | [004 SD・USBの切り分け](2026-09-20_004_sd_usb_diagnostics/README.md) | INA226通信成功、SD初期化失敗、Spresenseドライバー待ち |
| 2026-09-20 | [005 Spresense導入・転送](2026-09-20_005_spresense_bringup/README.md) | ドライバー・転送成功、取得アプリ起動未達 |
| 2026-09-20 | [006 Spresense起動原因](2026-09-20_006_spresense_bootloader/README.md) | 公式資料でブートローダー未導入に一致、書込承認待ち |
| 2026-09-20 | [007 親機SD・I²C・RTC](2026-09-20_007_sd_clock/README.md) | SD低速化で改善なし、RTC進行確認・時刻未設定 |
| 2026-09-20 | [008 Spresense公式導入再開](2026-09-20_008_spresense_boot/README.md) | 公式6パッケージ検証・取得アプリ起動成功 |
| 2026-09-20 | [009 IMU・GNSS・SD取得](2026-09-20_009_initial_sensors/README.md) | IMU取得を88→121Hzへ改善、SD検証成功。測位・統合同期は未達 |
| 2026-09-20 | [010 窓際GNSS・UART/PPS再検証](2026-09-20_010_window_link/README.md) | 衛星信号あり、測位・親機への通信と同期は未達 |
| 2026-09-20 | [011 IMU 960Hz・実サブコア並列化](2026-09-20_011_imu_multicore/README.md) | 平均967Hz、13,389行検証成功。厳密な等間隔性は未達 |
| 2026-09-20 | [012 配線修正後UART・PPS](2026-09-20_012_corrected_link/README.md) | 有効データ60件・PPS60回受信。UTC同期は未成立 |
| 2026-09-20 | [014 Sony SD保存形式・自動起動](2026-09-20_014_sd_autostart/README.md) | リセット・電源入れ直し後の自動保存・全行検証成功 |

```text
reports/
  YYYY-MM-DD_NNN_name/
    README.md          # 前提条件・実施内容・結果
    figures/           # グラフ・図
    evidence/          # 公開可能な根拠
  raw/                 # 生ログ（Git管理対象外）
```

[報告書テンプレート](../docs/REPORT_TEMPLATE.md)を使い、ピン配置・接続機器・未接続機器を毎回記載します。実測と模擬データ、実接続と予定の構成を区別してください。

- [親子実験操作画面のPages公開](2026-09-20_001_main_control_pages/README.md)

- [電池残量目安表示](2026-09-20_002_battery_estimate/README.md)

- [Spresense・GPS時刻同期の状態表示](2026-09-20_003_spresense_status/README.md)

- [スマホ位置・時刻共有](2026-09-20_004_phone_reference/README.md)

- [親機SDの復旧](2026-09-21_005_parent_sd_recovery/README.md)
- [016 GNSS衛星系・起動順序改善](2026-09-21_016_gnss_multisystem/README.md)：GNSS先行起動で3D測位成功、IMU・SD併用中も継続。

- [子機の再接続・IMU／SD／相対同期](2026-09-21_017_child_reconnect/README.md) — 再保存はCRC一致。旧ログ破損・センサー時刻逆転は未解決。

- [GPS UTC同期の実機確認](2026-09-21_018_utc_validation/README.md) — 相対通信復帰、GPS有効時刻・PPS未確認。

- [親子UTC同期・Wi-Fi時刻・RTC保持](2026-09-21_019_utc_rtc/README.md) — GPS親子同期、RTC保存・ソフト再起動復元を実機確認。Wi-Fi実受信・物理電源断は未確認。

- [Spresense保存形式の時刻確認](2026-09-21_020_spresense_time_format/README.md) — GPS UTCは保存済み、IMU行へのUTC付与は未実装。

- [021 Spresense IMUへのUTC追加・SD実機確認](2026-09-21_021_spresense_imu_utc/README.md)：IMU 5,825行とGPS 125行のCRC合格。GPS未測位のため有効UTC/PPS精度は未検証。

- [022 窓開放時のGPS・PPS・SD同時検証](2026-09-21_022_window_sd/README.md)：PPS継続中のSD異常は再現せず。正常停止でも親機に異常／未準備と表示される条件を確認。GPS未測位。

- [023 受信中のGPS・PPSとSD停止状態](2026-09-21_023_live_gps_sd/README.md)：GPS 3D・6衛星、UTC/PPS対応条件成立。SDエラー0、前回の正常停止が継続。

- [024 GPS受信後の連続記録・SDタイムアウト診断](2026-09-21_024_continuous_recording/README.md)：検証による自動停止を撤廃。実機で別のSD書込みタイムアウトを再現し、カード確認待ち。

- [025 SD挿し直し後の再検証](2026-09-21_025_sd_reseat/README.md)：書込みタイムアウトが再発。保存3,579行で異常停止、別カードでの切り分け待ち。

- [026 同じSDカードでの保存負荷切り分け](2026-09-21_026_sd_load/README.md)：45万行以上を保存、SDエラー0。GPS測位成立時の停止は未再現・未解決。
