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

```text
reports/
  YYYY-MM-DD_NNN_name/
    README.md          # 前提条件・実施内容・結果
    figures/           # グラフ・図
    evidence/          # 公開可能な根拠
  raw/                 # 生ログ（Git管理対象外）
```

[報告書テンプレート](../docs/REPORT_TEMPLATE.md)を使い、ピン配置・接続機器・未接続機器を毎回記載します。実測と模擬データ、実接続と予定の構成を区別してください。
