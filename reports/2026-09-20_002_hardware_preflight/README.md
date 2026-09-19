# 実機検証：接続確認

| 項目 | 内容 |
|---|---|
| 実施日時 | 2026-09-20 00:49〜00:51 JST |
| 目的 | 書き込み対象と計測可能な機器を識別する |
| 対象ソース | firmware: `821317766d7bf26252876a9c14f72dfe5324145d`、確認開始時HEAD: `fd9374c` |
| 判定 | 接続確認は一部実施、センサ・記録・精度検証は未実施 |

## 前提条件

**実際に検出したもの：** WindowsのCOM6にRaspberry Pi系USBシリアル機器1台。P_MAINのPico 2 Wであるか、搭載ファームウェアは未確認。SpresenseのUSBシリアルは検出されなかった。COM4/5はBluetooth仮想ポート。

**ユーザー申告の構成：** P_MAIN、電源基板、Spresense、IMU、両SD。今回の給電状態・IMU型番・SD容量/形式・実接続・基板版数は未確認。RTC、磁気センサ、ボタン、TX基板の接続状況も確認待ち。

**未接続（ユーザー申告）：** RX基板、水温、水圧センサ、テザー、LCD。

以下は設計上の接続であり、実配線を確認した結果ではない。

| 信号 | 接続元 | 接続先 | 設定・条件 | 実接続 |
|---|---|---|---|---|
| PPS | Spresense拡張D02 | J_SPR1.1 → Pico GP6・物理9 | 100Ω直列、3.3V | 未確認 |
| GPS UART | Spresense拡張D01 | J_SPR1.2 → GP7・物理10 | 115200、PIO受信専用、100Ω直列 | 未確認 |
| 共通GND | Spresense GND | J_SPR1.3 | 拡張JP1=3.3V、JP10 pin1–2開放 | 未確認 |
| 電源監視I²C | P-PWR INA226 | GP4/5・物理6/7 | 0x40、シャント20mΩ | 未確認 |
| SD SPI | P_MAIN SD | GP10/11/12/13・物理14/15/16/17 | SCK/MOSI/MISO/CS、1MHz | 未確認 |
| SD検出 | P_MAIN SD | GP15・物理20 | Low=挿入の想定 | 未確認 |
| LED / ALERT | 基板LED / INA226 | GP2/3・物理4/5 | LED出力 / ALERT入力 | 未確認 |
| ボタン | 基板ラダー | GP26・物理31 | 未押下約3.3V | 未確認 |
| TX制御 / 温度 | TX接続 | GP16/27・物理21/32 | 現ファームは制御Low、温度取得なし | 未確認 |

Pico USBのみでは基板の周辺電源V3Dを供給しない。P-PWRの5V/3.3V給電確認が必要。Spresense IMUはSony Multi-IMU、SDは拡張SDHCIを想定する。詳細と出典は[配線資料](../../docs/HARDWARE.md)。

## 何をしたか

1. PlatformIOのポート一覧とWindowsのシリアル機器一覧を照合した。
2. リムーバブルボリュームを列挙した。
3. COM6を115200bps、DTR/RTS無効指定で開き、10秒間受信のみ行った。コマンド送信・書き込み・ファーム更新は実施していない。

## 結果

| 確認項目 | 実測結果 | 判定 |
|---|---|---|
| Pico系USB機器の列挙 | COM6、USB VID:PID=2E8A:F00F | 検出 |
| COM6のオープン | 成功 | 接続可能 |
| 受動受信 | 10秒間、0バイト。送信0バイト | 機体・ファーム識別不可 |
| Spresense USB | 該当機器なし | 接続確認待ち |
| リムーバブルボリューム | 該当なし | PCからSD内容未確認 |
| IMU/GPS/SD/電源監視/時刻同期 | データ未取得 | 未実施 |
| 精度 | 比較基準・実測データなし | 未評価 |

USBが無出力であることだけでは故障とは判断できない。ボード上のSDがPCのドライブとして見えないこともSD故障を意味しない。公開根拠は[evidence/connection.json](evidence/connection.json)。空の受信生ログはGit管理外の`reports/raw/2026-09-20_002_hardware_preflight/COM6_passive.bin`に保存した。

```mermaid
flowchart LR
  PC[Windows PC] -->|COM6 開けた・受信0バイト| P[Pico系機器：親機か未確認]
  PC -. USB未検出 .-> S[Spresense]
  S -. 想定：D02 PPS / D01 UART .-> P
  W[P-PWR：給電未確認] -. 5V / 3.3V / I²C .-> P
```

## 未検証事項・次の確認

COM6の機体識別、Spresense USB接続、電源と実配線、接続機器一覧の回答を待って再開する。次に[実測順序と評価方法](../../docs/HARDWARE_VALIDATION.md)に従い、Spresense、Pico単体、統合時刻同期の順に実測する。それぞれの目的ごとに新しいレポートを作成する。
