# PPS同期・親機校正セットの実機配備

## 前提

- 2026-10-25 JST。ユーザーが「XHコネクタ左端がSpresense D02へ接続済み」と確認し、最後までの実機更新を指示した。途中でユーザーはGNSS受信場所を改善できると回答。その後、実機で測位成立を観測した。
- 親機J_SPR1は設計1番=PPS・2番=通信・3番=GND。include/pins.hと隣接AquaBeacon/pcbの親子ビルド資料を照合。D02は入力専用、親機GP6は外付けGNSS PPS、GP7–D03は既存通信を維持。AGENTS.mdを最新のユーザー確認へ合わせた。元のAquaBeacon/P_MAIN_experiment作業フォルダーは変更しない。
- 親機USB A444A41EE9334705 / COM6 / role=P_MAIN、Spresense USB 10878C89E6DBED1197F5F12D62C613AC / COM7 / IMUロガー応答を照合。子機は未接続・未書込み。親機温度/水圧/LCDは対象外。
- CADは未提供。配置や校正値を捏造しない。PC Wi-Fi切替、実機Web操作、SDフォーマット、既存ログ削除は行っていない。位置・無線情報を含む生ログはrawに限定する。

## 実施

1. 親機とSpresenseの記録を安全停止してから、親機とSpresense MainCore/logger・SubCore1/formatterを更新。最終SubCore1は75524byte、MainCoreは361176byteのビルド表示。両コアの共有ABIは3。
2. 最初のSpresense MainCore書込みは標準115200設定で進行停止。残った書込みプロセスの入出力カウンターが変化しないことを確認して停止し、旧ロガーの応答復帰を確認。921600の公式UploadSpeed設定で再試行し、パッケージ検証・nuttx保存・再起動を確認した。補助コアもパッケージ検証成功。
3. USB保守操作`@SET STATUS`/`@SET SAVE`を追加し、Webと共通の停止・カード・個別校正条件を検査。現在の配置がない場合は、確認済みフラグなしの下書きを新規保存する。
4. 親機の安全停止後BはArduino-Pico標準rebootへ変更し、再起動要求後に通常ループで給餌し続けないようにした。検証側ではUSB再列挙・長いJSONの行分割・SD再準備待ちを扱う。初期の判定期限切れを保存データ破損や復元失敗の証拠としない。
5. 実機でIMU時刻推定の再準備を観測し、周波数更新時に現在位相を連続に保ち、読出しの下側包絡を学習する方式へ修正。FIFOの連続読出しで単調な時刻を付けられない行は無効として保留し、全時計を毎回リセットしない。USBへIMU_TIME診断を追加した。
6. 既存の実CSV2035行では生タイムスタンプ差分が15594〜20686tickで、20000tick完全一致は2034差分全てに不一致だった。正常な揺れで6姿勢校正が毎回リセットされる条件を修正し、取得・校正で同じ周期窓を用いる。実際の全6姿勢校正を完了したものではない。
7. 磁気は10Hz変換に対するdata-ready確認を100ms→10msへ変更。LIS3MDL STATUS_REGを観測ログに残し、取得連番欠落と内部データ上書きを区別する。元のログ構造のサイズを保ち、有効u16の上位8bitへ状態を追加した。
8. SD再準備中、core0の選択UTCが古くなりSpresenseのUTC対応が一時失効する条件を観測。core1の最新GNSS PPSから配信UTCを直接生成するよう修正した。GPS無効化診断と非GNSSフォールバックは維持する。
9. 公開専用checkout P_MAIN_pages_publishでWebを更新・模擬試験・公開。既存の接続/role確認、API転送、オフライン保存を維持。生ログは公開していない。

## 実測・確認

- 更新前はGNSS通信中/未測位/PPS未検出、Spresense SDはPROBE_IO/errno=5。SDの初回空きは45056byte（44KiB）、認識記録領域は532676608byte。今回の追加実装より前からの状態であり、エラーを容量不足だけが原因と断定しない。
- 受信場所改善後にGNSS測位・親機PPS・UTC対応が成立。SpresenseでもD02 PPS検出とUTC対応成立を観測した。条件成立であり絶対位相・精度の合格ではない。
- 時計推定修正版（最終版と同じ時計コード）の240.152秒USB監視で、30秒以降の診断208件は全てquality=2、時計世代は0のまま、内部生カウンタの周回を1回確認。PPSキュー廃棄0。全960Hz行の保存確認ではなく、約1HzのUSB診断による継続状態確認。
- 校正セットを実SDへ保存し番号1を確認。配置確認フラグは全て0、個別校正参照は0。更新/再起動後に同じ番号がloaded=trueで復元。CAD登録済み/個別校正一致/精度合格とは表示しない。保存時親機session=1664939240と回収ログsession=2717262064が異なり、保存後の別起動での復元を確認した。
- 磁気ポーリング修正前の親機回収バイナリ32812byte：セット1、磁気227、GNSS25、HEALTH24。内外CRC不一致0、磁気取得連番欠落0、磁気/GNSS時刻順正常。磁気read区間最大1582µs。この値は変換時刻の不確かさではない。
- 最終版の再確認結果は同じrawフォルダーのparent_verification.json、parent_binary_verification.json、clock_soak_final.jsonへ保存。最終状態・件数を下記へ追記する。

## ソフトウェア・公開

- scripts/test_host.py成功：時計/周期窓/量子化読出し、親機セット保存/API/CRC、既存校正、新旧バイナリ、実formatterのschema3 CSV、画面・通信・保存復旧を確認。Pico parent/child100、Spresense両コアのビルド成功。子機実機は未操作。
- [公開画面](https://ry32767.github.io/P_MAIN_experiment/main-control/?v=20261025-v9)はHTTP 200・73794byte、画面commit 389714770a4dea7eb384fc254063e30efc3beb56のHTMLとバイト完全一致。[公開処理](https://github.com/ry32767/P_MAIN_experiment/actions/runs/40171754423)はsuccess。公開側結果の追記commitは9eb3d3b。
- 最終ファームのSHA256、書込みログ、モデル修正前後の生出力、親機バイナリ、公開HTMLと検査結果は[raw](../raw/2026-10-25_004_pps_deployment/)へ保存。主要ファイルはdeployed_hashes.json、upload_release_parent.txt、upload_release_logger.txt、upload_formatter.txt、host_release.txt、parent_verification.json、parent_observations.bin、clock_soak.json、public_verification.json。

## 精度の比較

センサが正式に対応した外部同期/共通クロックで960Hz取得を維持できるなら、サンプル時刻の対応は一般にそちらが有利。今回のPPSによる時計換算にはGPIO捕捉・ホスト読出し・IMU内部処理の未測定遅延が残る。一方、加速度/角速度のバイアスやノイズ、取付誤差そのものが外部同期だけで改善するわけではない。今回のSony Arduino 3.4.7公開IMU APIにはサンプリング回路をPPSへ直接ロックする操作を確認できないため、内部カウンタのPPS基準時刻換算を採用した。

時間ずれの影響の例として、仮に1msのずれがあれば一定角速度90度/秒では約0.09度、一定速度1m/秒では約1mmの時点差になる。Pythonで計算した仮定例で、本機の測定誤差ではない。

根拠：[Sony公式IMU取得例](https://github.com/sonydevworld/spresense/blob/master/examples/cxd5602pwbimu/cxd5602pwbimu_main.c)、ローカル公式SDK3.4.7のcxd5602pwbimu.h（rawへ保存）、[ST LIS3MDL仕様 §7.10](https://www.st.com/resource/en/datasheet/lis3mdl.pdf)。

## 未検証・残る条件

- Spresense SDは既存データを保持したまま、空き不足とPROBE_IOエラーにより保存不可。無理な再開や削除はせず停止する。空きのある正常なカードで、schema3の全960Hz保存・読戻し・長期負荷を確認する必要がある。
- CAD配置、全6姿勢/磁気の実機校正、電源断を伴う復元、PPS波形・絶対時刻差・固定読出し遅延・サンプリング位相、姿勢/位置精度は未検証。不確かさはUINT32_MAXの未測定のまま。
- 最終の実機Web操作・スマホのローカルネットワーク権限・キャッシュ更新はユーザー確認。PC Wi-Fiは変更していない。

## 最終版の結果

- 親機記録の停止・再開を含む120.132秒監視で、30秒以降のIMU診断89件は全てquality=2、時計世代0、PPSキュー廃棄0。SD処理中の時刻配信失効を修正した最終版でも継続を確認した。
- 親機の最終回収ログはセット1件・磁気232件・GNSS25件・HEALTH23件。内外CRC不一致0、磁気の取得連番欠落0、磁気/GNSSの時刻順正常。LIS3MDLの内部上書き状態も保存・デコードした。詳細な上書き件数はrawのparent_binary_verification.json。
- 最終状態は親機記録継続、セット番号1復元、GNSS測位/PPS/UTC対応成立、Spresense PPS/UTC対応・IMU時刻推定成立。Spresense記録はSTOPPED、SDはPROBE_IOの履歴あり。配置と個別校正は未完了の下書きであり精度合格ではない。
- 最終書込みはupload_parent_live_pps.txtとupload_release_logger.txt。ビルド/書込みSHA256はsource_deployment_manifest.json。最終のホスト検証はhost_live_pps.txt。

磁気内部上書きの検出は1件で、回収ログの先頭観測（index=0）だった。記録再開前の変換を含むため、取得連番欠落0を内部変換の全保存保証には読み替えない。先頭以降の観測では上書きフラグ0を確認した。

全USB監視終了後、親機からSpresense STOPを改めて要求し、要求番号に一致する成功応答とSTOPPEDを確認。最終のfinal_device_state.jsonでも親機記録継続、Spresense PPS/UTC対応・IMU時刻推定成立、セット番号1を確認した。
