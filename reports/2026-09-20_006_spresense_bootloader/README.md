# Spresense起動原因の確認・導入前チェック

| 項目 | 内容 |
|---|---|
| 日時 | 2026-09-20 01:47〜01:50 JST |
| 目的 | 取得アプリが起動せずupdaterに戻る原因を確認する |
| 対象 | Spresense COM7、Sony Arduino 3.4.7。開始HEAD `bb234ed` |
| 判定 | 原因の資料照合完了。ブートローダー導入は承認待ち・未実施 |

## 前提条件

両SD挿入・両USB接続はユーザー申告。今回COM6（親機）とCOM7（Spresense）を再検出した。Spresenseのアプリパッケージ転送・検証成功後も、前回の実測では`Welcome to nash (build 7bf64de)`と`updater#`へ戻る。SD型番、IMU型番、基板版数は未確認。

設計上の信号はSpresense拡張D02→J_SPR1.1→Pico GP6（物理9）、D01→J_SPR1.2→GP7（物理10）、GND→J_SPR1.3。3.3V・各100Ω、JP1=3.3V、JP10 pin1–2開放を前提とするが、実配線未確認。Spresense SDは拡張SDHCI、IMUはSPI5想定。RX、水温、水圧、テザー、LCDは未接続との申告。全体は[配線資料](../../docs/HARDWARE.md)。

## 何をしたか・結果

1. [Sony公式FAQ](https://developer.spresense.sony-semicon.com/development-guides/?lang=en&page=faq)で、上記のnashメッセージがブートローダー未導入を示すことを確認した。
2. [Sony公式Arduino手順](https://developer.spresense.sony-semicon.com/development-guides/?lang=en&page=arduino_set_up)のブートローダー導入とEULA確認を調べ、手元に対応する公式パッケージがあることを確認した。
3. Arduino CLIの公式burn-bootloader操作を要求したが、**自動承認審査が実行前に拒否**した。理由は「失敗時に起動不能になり得る高リスクな永続変更であり、この具体的操作への承認が不足」。コマンドは開始していない。別手段での迂回は行っていない。
4. COM7への公式ブートローダー書き込みについてユーザーへ明示的な承認を求めた。回答待ちの間に親機側の独立した検証を継続した。

## 残件

明示的承認後に公式ブートローダーを導入する。今回IMU・GNSS・SDの動作や精度は測定できていない。ブートローダー導入済みとは報告しない。
