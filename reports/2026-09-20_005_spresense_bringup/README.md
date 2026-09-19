# Spresenseドライバー再導入・転送確認

| 項目 | 内容 |
|---|---|
| 実施日時 | 2026-09-20 01:34〜01:45 JST |
| 目的 | 管理者確認を再試行してUSB通信を確立し、取得プログラムの転送・起動を確認する |
| 対象 | Spresense、CP2102N、COM7 |
| 対象ソース | Spresense firmware `821317766d7bf26252876a9c14f72dfe5324145d`。開始時リポジトリHEAD `68579db` |
| 判定 | ドライバー・USB通信・パッケージ転送成功。取得プログラム起動未達 |

## 前提条件

両基板はUSB接続、両SDは挿入済みとのユーザー申告。親機はCOM6。SpresenseのIMU型番・基板版数・SD型番/容量/形式は未確認。電源基板のINA226通信は前回成功したが、今回電源精度は測っていない。RX基板、水温、水圧、テザー、LCDは未接続との申告を継続適用。

| 接続 | 設計条件 | 今回の確認 |
|---|---|---|
| Spresense USB | メインボードUSB → PC | CP2102NがCOM7として正常認識、双方向応答 |
| Spresense SD | 拡張SDHCI | 挿入申告あり、読み書き未実施 |
| IMU | Sony Multi-IMU、SPI5想定 | 型番・取得とも未確認 |
| PPS | 拡張D02 → J_SPR1.1 → Pico GP6（物理9） | 3.3V、100Ω、実信号未確認 |
| UART | 拡張D01 → J_SPR1.2 → Pico GP7（物理10） | 115200、3.3V、100Ω、実信号未確認 |
| GND | Spresense → J_SPR1.3 | 実配線未確認、JP1=3.3V/JP10 pin1–2開放が前提 |
| 親機SD | GP10/11/12/13（物理14/15/16/17） | 前回ACMD41失敗、今回再検証なし |
| 親機電源監視 | GP4/5（物理6/7）、INA226 0x40 | 前回通信成功、今回再検証なし |

その他のピンと接続条件は[配線資料](../../docs/HARDWARE.md)。TX GP16は親機ファームでLow固定。

## 何をしたか

1. 取得済み公式CP210xドライバーの署名Validを再確認し、ユーザー指示に従ってWindows管理者確認を再表示した。
2. ドライバー導入終了コード0と、COM7/Status=OKを確認した。
3. Arduino CLI経由の公式Windows書込ツールを試したが、1K転送の進捗が止まったため中断。115200bps明示でも同様だった。
4. 公式Python版を試し、必要なwxPythonをプロジェクト内環境へ導入した。Python 3.13で削除された`collections.Callable`参照を実行時に`collections.abc.Callable`へ対応付けた。公式ソースは変更していない。
5. ボードの`install`応答でXMODEM CRC/1K対応と開始要求`C`を確認。公式同梱XMODEM実装の128Bモード、115200bps、応答タイムアウト3秒、再試行上限3で転送した。Windows版の待機処理は使用せずpyserialで送受信した。
6. 転送後のボード検証メッセージを確認し再起動。更新用コンソールへ戻るため、公式スクリプトにある`set bootable M0P`を試したが拒否され、設定は変更されなかった。`set`で現設定を読み取った。

## 結果

| 項目 | 実測結果 | 判定 |
|---|---|---|
| ドライバー | 11.6.0.420、導入終了コード0 | 成功 |
| USB | Silicon Labs CP210x USB to UART Bridge (COM7)、Status OK | 成功 |
| コンソール | `Welcome to nash (build 7bf64de)`、`updater#`、help/set応答 | 双方向通信成功 |
| 128B転送 | 1403ブロック、途中再送カウント1、transfer_ok=True | 転送完了 |
| ボード検証 | `179552 bytes loaded.` / `Package validation is OK.` / `Saving package to "nuttx"` | パッケージ検証成功 |
| 再起動後 | 更新用コンソールに戻る | 取得アプリ未起動 |
| 起動設定の試行 | `M0P is not valid bootable. config not changed.` | 拒否、設定変更なし |
| 現設定 | boot mode=auto、bootdelay=100、bootable=loader | 読取結果。bootdelayの単位は未評価 |
| IMU/GPS/SD/時刻同期 | 取得プログラム未起動 | 未実施、精度未評価 |

```mermaid
flowchart LR
 A[管理者確認を再試行] --> B[ドライバー成功・COM7]
 B --> C[USB双方向通信成功]
 C --> D[128B転送・パッケージ検証成功]
 D --> E[再起動でupdaterへ戻る]
 E --> F[起動用ファームの状態確認が必要]
```

公開根拠：[evidence/result.txt](evidence/result.txt)。生ログはGit管理外の`reports/raw/2026-09-20_005_spresense_bringup/`に保存。今回起動用ファームの書き換え・リカバリー・SDフォーマットはしていない。

## 残件

ドライバー再導入の依頼は完了。取得アプリを起動するには起動用ファームの状態・必要な初期セットアップを確認する必要がある。`nuttx`の転送成功だけでは起動用ファームやGNSSファームが揃っていることを保証しない。IMU・GPS・SDの動作と精度、親機との統合試験は未完了。

参考：[Sony公式セットアップ](https://developer.spresense.sony-semicon.com/development-guides/?lang=en&page=arduino_set_up)、[CP210x公式ドライバー](https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers)。
