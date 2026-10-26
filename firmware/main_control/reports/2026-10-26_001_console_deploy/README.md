# 未接続の赤表示・親機への実機書込み

## 前提
- 2026-10-26。ユーザーが未接続の赤表示と実機書込みを指示。
- 子機は未接続。USB番号とファーム応答の両方で親機を識別し、親機のみ更新する。
- PC Wi-Fi切替、SDフォーマット、既存ログ削除、元リポジトリ・配線変更は行わない。実機Web画面はユーザーが確認する。

## 実施
- 状態カード・SDカードは、最初から未接続/未取得でも赤表示に変更。初期HTMLも赤とし、接続が確認できて異常がなければ通常色へ戻す。
- 初回未接続と接続後の通信途絶は文言を区別する。操作無効化と詳細の開閉保持を維持する。
- ホスト試験に初期未取得・未接続の赤表示、復帰時の解除を追加。README・spec・DESIGNを同期。
- 親子ビルドとホスト試験を実行。scripts/flash.py parentで再識別・Xqによる記録/測距停止後、前日のRTK/状態/校正/時刻位置/SD改善を含む親機ファームを書き込んだ。
- 書込みはフラッシュ照合100%・SUCCESS。親機が再起動した。Spresense/子機のファームは書き込んでいない。

## 検証結果・実測
- 書込み前：COM6、USBシリアルA444A41EE9334705、h応答P_MAINを確認。SD reason=NONE、記録中、SDエラー0。子機未接続、Spresense通信あり・記録停止。
- ビルド：parent / child100成功。静的RAMは親機391088 byte（74.6%）、子機273460 byte（52.2%）で前回012から増加なし。親機Flash604224 byte。
- `../P_MAIN_experiment/.venv/Scripts/python.exe scripts/test_host.py`全件成功。埋込みHTMLとソースの一致、書込み対象のSHA256を保存した。
- 再起動直後の最初のUSB確認は25秒間受信0 byte。原因は未確定。ポートを閉じて接続し直した次の確認ではP_MAIN応答を取得できた。追加のリセット・再書込みは行っていない。
- 更新後25秒間で49件の親機状態応答。SD reason=NONE、記録中、エラー0。新規P_MAIN_TIME_POSITION_00053.bin、行数28→53、GNSS RAWX保存30→54。PPK記録中。
- Web AP起動true、Web待受けtrue、AP IP=192.168.4.1。末尾のUSB診断heap_free=105112 byte（瞬時値）。テザリングSTAは未接続、status=4、IP未割当て。Web到達成功とは扱わない。
- 更新後の実USB状態をUI関数へ入力するPC試験で、未接続子機の状態/SDカードが赤になることを確認した。
- Spresenseは前後とも通信あり、SD state=ERROR / failure=WRITE / detail=110 / SDエラー5 / 記録停止。今回の更新前から存在する状態で、復旧操作は行っていない。

## 未検証
- スマホでの実機Web表示・再読込み・操作、テザリング復旧、Spresense SD復旧、未接続子機の動作。
- RTK受信・前回局の実SD保存/再接続、長時間安定性、最大ヒープ/スタック余裕、時刻・位置・距離精度。
- 初回USB無応答の原因は未確定。再接続後の25秒の確認を長時間安定性の証明とはしない。

## 証跡
`reports/raw/2026-10-26_001_console_deploy/`：変更前ソース、build.txt、host.txt、flash.txt、before/afterのUSB生ログ・JSON・summary、after_first_unresponsive、verification.json、device_ui.json、sha256.json。

## ユーザーの画面確認
スマホ側でコンソールを再読み込みし、未接続子機が赤いことを確認する。親機へ直接接続している場合のURLはhttp://192.168.4.1/。PCのWi-Fiを切り替える必要はない。
