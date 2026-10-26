# Spresense双方向通信・ソフトウェア校正の完成版

## 前提

- 最新の確定配線: 親機GP7–Spresense拡張D03、共通GND、JP1=3.3V。D02未接続。ユーザーの「全部繋げた」に対してPPS追加有無を質問したが、回答はまだないため、追加PPSを仮定して出力設定しない。
- 親機GP6は既存GNSS PPS入力を維持。include/pins.hと隣接AquaBeacon/pcb/build_parent_main.py / build_child_main.pyを照合。元ソースは変更していない。
- 親機USBシリアル番号A444A41EE9334705・応答role=P_MAIN、Spresense USBシリアル番号10878C89E6DBED1197F5F12D62C613AC・IMUロガー応答を確認。記録停止/formatter待機を確認してから書込み。
- PC Wi-Fi切替なし。実機Webはユーザーが確認する。親機温度/水圧/LCDは対象外。

## 実施した実装

- GP7–D03でLOW出力/解放だけを使う1250bps・8N2・128byte CRC32の半二重通信。親機から参考UTC/操作、SpresenseからIMU/SD/記録/校正の状態を返信。
- 状態取得、記録停止/新規ファイルで再開、安全停止後のリセット、校正開始/姿勢選択/状態/保存/終了/解除を実装。Web画面とUSB操作に対応。
- 6姿勢を各2秒整定＋10秒静止し、加速度オフセット/倍率と地球自転を残すジャイロバイアスを計算。これはSony専用ツールとは別のソフトウェア補正であり、IMU内部係数には書かない。
- SDへ新規TMPを排他作成、CRC/同期/読戻し後に新規CALとして確定。保存/復元/無補正レコードによる解除。失敗時は前の適用値を保つ。既存ログ削除・フォーマットなし。
- Ixxxxx.CSVには生の値と補正値、校正番号を併記。Txxxxx.CSVには単調時計/参考UTC/時刻源を保存。MainCore/SubCore1共有バッチの版も更新。

## 実機で見つけて修正した問題

- 受信タスクが10〜20ms待つと、処理時刻での区間判定が受信フレームを途中で捨てていた。ISRで各byteの受信時刻を保存して判定するように修正。ホストでも100ms間隔の遅延消費を再現し検証した。
- SDKのCLOCK_MONOTONICは10ms粒度。校正の約1msサンプル判定に使わず、Sony Arduino 3.4.7の64bit micros()（32768Hz電源投入後RTC）を使用。
- SD未認識時のキュー充填/記録中表示を止め、再認識を進める。SD処理より先に通信タスクを開始する。送信待ちの期限を追加。
- 2500bpsで操作応答の失敗を観測したため1250bpsへ調整。操作応答が失われた場合は同じセッション/連番/全ペイロードを最大2回再送し、Spresenseのキャッシュで重複実行を防ぐ。最終期限切れは実行結果不明。

## 実測結果

| 項目 | 結果・根拠 |
|---|---|
| 親機書込み | 成功。[ログ](../raw/spresense_completion_parent_final_upload.txt) |
| Spresense MainCore / SubCore1書込み | 両方成功。[Main](../raw/spresense_completion_logger_deployed.txt) / [Sub1](../raw/spresense_completion_formatter_upload.txt) |
| 双方向通信 | 操作試験の親機受信47→62packet。CRC/framingエラー0、最終期限切れ20→20で増加なし。[結果](../raw/spresense_completion_controls.json) |
| 参考UTC | 親機でtime_valid=true / time_source=1。Spresense側でもutc_source=1を確認。精度は未測定 |
| 既存親機GNSS PPS | 更新後もpps_fresh / sync_candidate成立を観測。[生ログ](../raw/spresense_completion_parent_timestampfix.txt) |
| 記録停止 | 親機から要求→Spresense受付→last_operation=2 / last_result=0、recording=falseを確認 |
| 校正操作 | 開始、+Z姿勢の測定開始、終了にOK応答。未保存の候補による適用番号変更なし |
| 記録再開 | 受付後、Spresense SD未認識のためlast_result=4（失敗）。recording=false / sd_ok=false。成功とは判定しない |
| 安全リセット | 停止後に再起動を要求。IMUサンプル数36219→2051へ戻り、通信/IMU取得が復帰。受付だけではなく再起動を確認 |
| 保存・解除の失敗処理 | 6姿勢未完了での保存はINVALID。解除はSD未認識でFAILEDとなり、補正番号0・未適用を維持。[結果](../raw/spresense_completion_failure_guards.json) |
| 最終状態 | 受信119packet、CRC/framing/collision/overflow各0。校正セッションなし・補正番号0・記録停止完了。保存/復元エラー表示はSD未認識によるもの。親機PPSもfresh。[状態](../raw/spresense_completion_final_state.json) |

SD初期化はBOOT_OPEN / RECOVERY_OPEN / errno=2で失敗。SDカードがSpresense拡張側に挿入済みか質問中。親機側SDとは別。未認識の原因をカード未挿入と断定していない。

## ソフトウェア検証

- Pico parent / child100ビルド成功。[ログ](../raw/spresense_completion_pico.txt)。親機の最終再ビルドは書込み済みbinとSHA256一致。
- Spresense MainCore logger 302600byte、SubCore1 formatter 74972byte、ビルド成功。[ログ](../raw/spresense_completion_build.txt)。両コアとも最終再ビルドと書込み済みパッケージのSHA256一致。[書込み済みハッシュ](../raw/spresense_completion_deployed_hashes.json)。
- ホスト試験成功: CRC/再同期/重複抑止/受信時刻/期限/4位相UART/LOW固定・あふれ、6姿勢計算・北南緯度・地球自転保持・静止/欠落拒否・CRC・解除・生/補正CSV。[ログ](../raw/spresense_completion_host.txt)。
- オフライン画面試験成功: 古い状態での操作禁止、停止前リセット禁止、保存前の6姿勢完了条件、受付と完了/期限切れの区別。[Spresense](../raw/spresense_completion_ui.txt) / [全コンソール](../raw/spresense_completion_console.txt)。
- 保存CSV/校正レコード検査ツールを合成データで確認。実際のSpresense保存ファイルはSD未認識のため未確認。

## 未検証・残る実機作業

実際の全6姿勢測定、校正値のSD保存/電源再投入による復元、補正解除レコード保存、960Hz補正併記ログの実負荷、実機Web、GPIO波形、絶対時刻/距離/IMU精度は未検証。Sony専用ツールと同じ内部校正や温度/軸間行列の完全補正は実装対象外。

SD認識後は「停止→地点の重力/緯度を設定→6姿勢測定→保存→記録再開」の順に確認する。コードの実装・模擬試験・操作通信の実測と、精度合格を混同しない。

詳細: [通信・ソフトウェア校正仕様](../../docs/spresense_halfduplex.md)。
