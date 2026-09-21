# 親子統合ファームウェア

P_MAIN / C_MAIN Pico 2 Wの実機検証用コード。従来の`firmware/pico`とは別の親子統合構成です。

## 開発

Pythonにplatformio、pyserial、ziglangを導入し、このフォルダで`pio run -e parent -e child100`を実行します。`python scripts/test_host.py`でホストテスト。ソースに同梱するBNO08xライブラリは全イベント取得対応の専用コピーで、元のライセンスを維持しています。

書込み補助scripts/flash.pyは今回のUSB個体IDを固定照合します。別個体に流用する場合は識別設定を見直してください。初回書込みは既存ファームの役割応答も必要です。

## UTC同期・RTC保持


時刻源は有効なGPS PPS候補 → Wi-Fiで接続したスマホ/端末の時計 → DS3231 RTCの順。機体内Web画面を開くと端末時計を約30秒周期で共有する。位置情報は従来どおり明示操作。AP単体はインターネット時計を取得しないため、スマホ/PCの自動日時設定が必要。PCのWi-Fi切替は行わない。

UTCは取得・測距用の単調時計を変更せず、別の基準時刻として管理する。親機は既存の相対同期後、空き時間に同じ130バイト長のUTC_ANCHORフレームを送る。子機は親子offsetで基準の単調時刻を変換し、UART受信遅延をUTCへ加えない。セッション・要求番号・相対同期を検査し、4秒で受信基準を失効。無通信時は子機自身のRTCへ戻る。混在する旧ファームはUTCを扱わないため両機を更新する。

RTCはI2C0の0x68、SDA GP4 / SCL GP5。取得後の初回・時刻源変更・10分ごとに保存し、読戻しで確認。失敗時は5秒以上空けて再試行する。OSF（発振停止履歴）、BCD、日付、2020–2099年の範囲を検査し、無効なRTCを時刻源にしない。電池動作中の発振を有効化する。RTCは秒単位で書込み、位相・絶対精度は保証しない。電源断保持には有効なバックアップ電池が必要で、ソフト再起動試験と物理電源断試験を区別する。仕様根拠：[DS3231公式データシート](https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf)。

子機の定周期IMUと全BNOイベントCSVへutc_us/utc_sourceを追加。未同期は0/none。ヘルスCSVへUTC基準の単調時刻・epoch・有効期限・時刻源を追加。親機の時刻ログにも実際の選択時刻源を保存。時刻源変更や校正でUTCは飛ぶことがあるため、経過時間・サンプリング評価はscheduled_us/acquired_usを使う。GPS候補・端末時計・RTCを混同しない。

Web/USBはutc.valid、source、rtc_valid、rtc_writes、rtc_write_errorsを表示。子機のUTC・RTC状態を親機ヘルス経由でも表示する。utc_validは基準時刻を利用可能という意味で、絶対精度の校正合格ではない。共有停止は端末からの更新停止であり、既存RTCとSDログは消さない。

GPSフレームのPPS後下限は20msから2msへ変更。64バイト/115200bpsの転送は約5.56msで、実機は約10–16ms後に届くため旧条件では排除されていた。上限800ms、PPS周期、連続3エポック、時刻有効性の検査は維持。PC/NTPとの粗い照合と外部パルスによる高精度校正は別。

USB診断（通常実験では不要）：FはGPSの選択を90秒だけ除外、Gは診断除外を解除、Jは親機のUTC配信だけ15秒停止。自動解除され、状態utc_testに表示する。BはSDがSTOPPEDかつ転送中でない場合に限りソフト再起動する。F/Jは通信・PPSそのものを物理遮断する試験ではない。

検証コマンド：`pio run -e parent -e child100`、`python scripts/test_host.py`。実機書込みは`python scripts/flash.py parent` / `child100`（個体と役割を照合して安全停止後に書込み）。
