# GNSS L5有効化・補正採用診断・PPK保存キュー改善

## 前提
- ユーザー依頼：「今できる範囲でできること」。直前の貝谷大池実験で実機RTK Fixedなし、PPKの一部Fixedも整合性不足。
- 実験データdata/は保持。元GNSSログのSHA-256不変を確認。PC Wi-Fi切替、SDフォーマット、ログ削除なし。
- USB列挙のCOM6 / A444A41EE9334705と、h応答role=P_MAINを照合。書込みはscripts/flash.pyによる再識別・Xq安全停止後。子機・Spresenseへの書込みなし。
- 作業時に親機SDはCARD_NOT_PRESENT。PCへコピー後にカードが抜かれた状態であり、既存記録への書込みは行っていない。
- 組込みファームウェアスキルを使用。ピン配置は変更せず、GP6外付けGNSS PPS入力、Spresense D02入力、GP7–D03半二重を維持。

## 原因の追加確認
最初に診断だけを配備して受信機RAMを読んだところ、**GPS有効=true、GPS L1有効=true、GPS L5有効=false** を確認した。Galileo E1/E5aは有効、DGNSSMODEは3（FIX試行）だった。GPS L5無効は現地ログにGPS L5観測が全くなかったことと整合する。

これでGPS L5未観測に対する設定上の問題を1つ特定できた。ただし、この設定だけが現地の未FIXを起こした唯一の原因とは断定しない。今回の基準局はGPS L5を配信しておらず、Galileo受信品質の不足も残る。

## 実施した変更
### GPS L5
- 正にF9Pと識別し、GPS/L1有効かつGPS L5無効とRAM読戻しできた場合だけ、初回補正送信前にGPS L5をRAM上で有効化。
- 親機起動中1回のみ試行。ACK/NAK、2秒応答期限、ACK後700 ms待機、再読戻しを実装。信号変更でGNSSサブシステムが再起動する間は追加コマンドと補正送信を待機。
- ACKだけでは成功扱いしない。有効化試行/応答/失敗/読戻し確認を分離して公開。
- 受信機Flash/BBR、他信号の設定、衛星health判定は変更しない。電源断で受信機の設定が戻っても、次の親機起動時に確認・適用する。

### 診断と記録
- GPS有効/L1/L5、Galileo有効/E1/E5a、RTK動作モードを30秒ごとにRAM読戻し。
- NAV-SIGを5秒ごとに照会。追尾GPS L5信号数、Galileo E1/E5a、同じ衛星で2周波位相を使用中の数、位相補正使用数を集計。
- RXM-RTCM出力を有効化。受信機が採用/不採用/不明とした補正件数と、受信機側CRC不良を集計。
- 設定読戻し、NAV-SIG、RXM-RTCMをPPKのUBXへ追加保存。衛星別C/N0・補正元・位相使用フラグを原文で保持する。
- 設定60秒、衛星/補正15秒で表示を更新途絶扱いにする。未取得と0/無効を区別。USB/JSONL/Web表示に反映。
- config_verifiedは通信・所定出力設定の確認。信号設定、補正採用、FIX、精度の合格とは別。

### 保存欠落対策
- 8192 byte固定枠×8のGNSSキューを、可変長レコード用64 KiB SPSCキューに置換。短い航法メッセージ集中時の無駄を減らす。
- RTCM保存キュー16→32枠。GNSS排出を最大16件・4 ms目安、RTCM排出を最大8件へ変更。単独のSD書込みに時間がかかれば4 msを超えるため、厳密な処理時間上限とはしない。
- キュー現在byte・起動後最大byte、セッション別RAWX/SFRBX/その他の投入失敗数を追加。既存の総droppedはSD不使用時の排出失敗も含むため、種類別投入失敗とは意味が異なる。
- FIFOはフレーム全体をコピー後に公開し、消費完了後に領域を解放。uint32カウンターの周回も検証した。
- README.mdとdocs/spec.mdを同時更新。新規docsなし。

## 検証
### ビルド・ホスト
- 指定のPlatformIO既定ビルド：parent / child100とも成功。最終ログbuild_final.txt。
- 依存入りPythonでscripts/test_host.py全体が成功。最終ログhost_l5.txt、終了コード0。Wi-Fiを切り替える実機Web試験は実行していない。
- 実際の診断デコーダで、RAM層/バージョン/長さの検査、未知キー、壊れた応答での旧値保持、NAV-SIGの衛星別2周波対応、RTCM採用フラグとCRC失敗の扱いを検証。
- L5有効化方針：既に有効なら変更しない、不明なら変更しない、ACK/NAK/タイムアウト、700 ms境界、millis周回、読戻し成功を検証。
- 実際のpollGnssを模擬UARTで実行し、元の設定読戻し後も診断用VALGETでconfig_verifiedを誤って解除しないこと、L5無効の検出→RAM設定→ACK→再読戻し、診断フレームの保存キュー投入、補正転送、autobaud時の古い診断破棄を確認。
- キュー：短い128フレーム、最大サイズ、満杯時の拒否、配列境界/カウンター周回、2スレッド・20000レコードの並行push/popで内容と順序を確認。
- 実画面コードのオフライン試験で不明/有効、Floatのみ/FIX試行、補正診断の期限切れ、欠落数表示を確認。

### 最終実機
- scripts/flash.py parentで最終版の書込み成功。
- 25秒のUSB観測、P_MAIN診断26件：GPS L5有効=true、L5有効化ACK=true、読戻し確認=true、設定失敗=falseを全件で確認。
- **GPS L5の実追尾1〜2信号** を確認。設定だけでなく追尾が発生した。
- Galileo E1/E5a有効、RTK動作モード3。UART overflows=0、UBX errors=0。
- NMEA CRCエラーは観測開始時から累計2で、その区間で増加なし。GNSS信号変更の起動過程全体でNMEAエラー0とは主張しない。
- 補正受信は停止中でRXM-RTCM件数0、status_fresh=false。0を「補正不採用」と解釈しない。
- SDカードなし、PPK recording=false。キュー使用0/欠落0は記録していない状態の値であり、実機SD保存の合格ではない。

## 未検証・残る対応
- 新しい診断を含む実機SD保存・長時間の欠落低減、補正受信中のRXM-RTCM採用診断、ユーザーによる実機Web表示確認。
- GNSSと親機を両方完全に電源断した後の再適用。実装上はRAMのみであり、受信機永続保存済みとは扱わない。
- 屋外のGPS L5追尾継続、共通2周波衛星数、RTK FIX成立、既知点位置精度の改善。
- 基準局は変更していない。現行3C722D56のGPSはL1/L2であり、GPS L5有効化だけではGPSの共通2周波補正は増えない。GPS L1/L5を配信する適切な局の現地比較が必要。
- アンテナ設置・高さ・位相中心、基準局座標系/元期の確認も残る。既存現地データの精度値は今回の変更で改善しない。

## 操作
1. SDを戻す場合は、記録停止と電源断後に戻し、再起動する。フォーマットや既存ファイル削除は不要。
2. 時刻・位置画面でGPS L5設定が有効、追尾信号数が増えることを確認。有効化した起動では「読戻し確認済み」、既に有効なら「変更なし／未確認」表示と設定有効を併せて見る。
3. 屋外で補正を開始し、「補正受信件数」と「受信機での補正採用」を別々に確認する。新しい実験ファイルでRAWX/SFRBX/その他の投入欠落を確認。
4. アンテナを固定して既知点を再測定し、前後PPK一致と公表座標との差で評価する。

## 証跡
reports/raw/2026-10-25_009_gnss_improvements/ 以下：
- before/、changes.diff、final_manifest.json：変更前/差分/最終ソースとUF2のSHA-256。
- before_status.txt、after_status.json：L5無効を確認した診断版。
- after_l5_status.txt/json、verification.json：有効化・追尾の最終実測。
- build_final.txt、host_l5.txt、flash_l5.txt：最終ビルド・試験・識別付き書込み。
- tests/test_gnss_diagnostics.cpp、tests/test_gnss_link.cppは本体と同じデコーダ・pollGnssを実行。

一次資料：[u-blox F9 HPG L1L5 1.40 Interface Description UBX-23006991 R02](https://docs.sparkfun.com/SparkFun_u-blox_NEO-F9P/assets/component_documentation/u-blox-F9-HPG-L1L5-1.40_InterfaceDescription_UBX-23006991.pdf) のVALGET、NAV-SIG、RXM-RTCM、CFG-SIGNAL、CFG-NAVHPG。信号設定変更後の待機は同資料のACK後0.5秒以上に対し700 msを採用。
