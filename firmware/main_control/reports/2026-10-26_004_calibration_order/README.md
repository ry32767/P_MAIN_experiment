# 校正時刻の記録確認・校正画面の実施順

## 前提
- 2026-10-26 JST。校正時刻をカメラ内IMUとの時刻同期へ使えるかの確認と、校正画面の並び替え。
- 今回はコード確認・ローカル画面変更・PC検証のみ。実機USB操作・校正・書込み・公開は行っていない。PC Wi-Fi切替、SDフォーマット・既存ログ削除、隣接リポジトリ変更なし。
- 親機温度/水圧/LCDは対象外。ピン・通信フレーム・保存形式を変更しない。

## 時刻記録の確認結果
| 記録 | 現行の挙動 |
|---|---|
| Spresense通常IMUログ | received_mono_us、生センサ時刻、sample_mono_us、sample_utc_us、品質、時計世代、校正番号等を記録。UTC不成立は0、不確かさは未測定 |
| 子機通常BNOログ | acquired/source/utc/utcSourceを記録。取得時計とセンサ内部時刻を区別する |
| 校正中のIMU波形 | 保存しない。Spresenseは停止中のみ校正可能でimuQueueへの追加はrecording時のみ。子機は校正開始前に記録停止し、captureEnabled=falseで通常保存を止める |
| 各姿勢の開始・終了と個別校正UTC | アプリとして永続履歴を持たない。RAM内の単調時刻や経過時間はあるが保存形式へ入らない |
| 親機セット | createdUtc/utcSourceを保存。有効UTCがない場合は0/時刻源なし。個別校正の実施時刻ではなくセット作成時刻 |

根拠：firmware/spresense/logger/logger.inoのimuTaskと校正要求ガード、common/spresense_calibration.h、common/mag_calibration.h、common/bno_calibration.h、common/binary_log.h、src/main.cpp、include/parent_calibration_store.h。

カメラと同じ機体に固定したIMUの共通運動を使う照合は候補になるが、カメラ内IMUのサンプル時刻とデータ抽出が必要。現行の校正記録だけでは不足する。今回は校正後の通常ログと撮影に照合用運動を別途残す手順を案内し、専用収録・自動照合は追加しない。各カメラと同じ機体のIMUを組にし、親子別々の動きを直接照合しない。絶対UTC精度を認定しない。

一次資料：[ETH Kalibr Camera IMU calibration](https://github.com/ethz-asl/kalibr/wiki/camera-imu-calibration)。同資料は画像とIMUの時空間校正、各軸の運動、時刻品質を扱う。今回のカメラ内IMUとの波形照合の実証ではない。カメラ機種・IMU抽出方法はユーザーへ照会中で、画面変更には依存しない。

## 実施
- web/index.htmlを準備→1親機Spresense IMU→2親機LIS3MDL→3子機BNO→4親機セット保存→5復元確認・記録再開へ変更。子機は独立・未使用時は省略可能と明記。
- 個別校正の後に毎回記録再開する説明を、最後の記録再開へ統一。カメラ同期用の別収録と現機能の限界を追記。
- 既存操作ID、接続/停止ガード、detailsの開閉保持、色・書体・レスポンシブCSSを維持。完了判定や自動進行は追加しない。
- README、docs/spec.md、docs/parent_calibration.md、web/DESIGN.mdを同期。docsの増減なし。ビルドでinclude/web_page.hを再生成。

## 検証結果・実測
- 指定環境のscripts/test_host.py終了コード0、既存ホスト試験全件成功。
- PlatformIO parent/child100ビルドともSUCCESS。ファーム書込みは行っていない。
- structure.jsonでDOMの案内順、既存全IDとID付き操作要素の保持、時刻記録の制限文、埋込みHTMLとソースの完全一致を確認。個別操作の送信先や異常時ガードは既存模擬試験で確認。
- 127.0.0.1限定の既存模擬サーバーで校正タブを開き、準備・番号付きの4項目・復元確認とカメラ案内をブラウザーで確認。スクリーンショットの保存済み/一致等は全て模擬値で実機の校正結果ではない。
- 新規CSS/入力/ボタン/画像はなし。既存ホスト試験でID一意、ラベル、5タブ、構文、色コントラストを確認し、44px操作領域・フォーカス・単列用CSSの保持を静的確認。375pxの新規スクリーンショットは取得していない。
- 新規センサ実測・カメラ同期精度測定はなし。

## 未検証
- 実機への反映・スマホでの実機Web表示。PC Wi-Fiは変更せずユーザーが実機Webを確認する。
- カメラ内IMUと映像の時間対応、機体IMUとの時刻差/ドリフト推定、取付ずれ、固定遅延、同期精度。
- 実機校正の完了・再起動復元、Spresense SD障害の復旧、連続記録の安定性。前回監査の未完了事項を引き継ぐ。

証跡：reports/raw/2026-10-26_004_calibration_order/ 以下のbefore、host.txt、build.txt、structure.json、calibration-preview.png。
