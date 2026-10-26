# 親機校正セット・PPS基準サンプルログ

## 前提

- 2026-10-25 JST。ユーザー依頼はSpresense/IMUのPPS同期、取得時刻・連番付き記録、既存センサー校正タブ内の親機校正セット。
- 親機J_SPR1は設計上1番PPS・2番通信・3番GND。ユーザーは3本のうちPPSが接続されていたはずと申告。Spresense側終端は未確認のため、D02入力専用を実装し、未検出時は未同期とする。親機GP6/GNSS PPS、GP7/D03の既存通信は維持する。
- include/pins.hと、その参照先common/spresense_pins.hをピン定義に使用。隣接AquaBeacon/pcbのbuild_parent_main.py/build_child_main.pyを読取り照合した。現物配線照合は未実施。旧AGENTS.mdのGP6/GP7記述との不一致は残るため、書込み前に解消する。
- CADは今回未提供。架空の取付校正値を保存・適用しない。親機温度・水圧・LCDは対象外。
- 実機USB接続・給電操作・書込み・再起動・実機校正は行わない。PC Wi-Fi切替、実機Web試験、SDフォーマット、既存ログ削除、隣接リポジトリ編集、GitHub Pages公開は行っていない。

## 実施

- Spresense D02 PPS割込み捕捉、親機GNSS PPSラベルの既存D03配信、連続対応・失効・異常周期・重複・キュー溢れの検査を追加。PPSの番号選択に公称通信時間を使い、位相と倍率は捕捉PPSを基準にする。UART受信byteの時計はタイマー起動後カウントからMCU単調時計の実捕捉へ変更した。
- IMUの内部カウンタを64bitへ展開し、ホスト読出しとの時計倍率/オフセットを推定。各行にサンプル推定時刻、PPS対応UTC、品質、時計世代、親機セット番号/sessionを追加。生値・補正値・連番・読出し時刻を維持。開始後50msはSony仕様に従って除外。PPSアンカーの診断をTxxxxx.CSVにも追加した。
- IMUサンプリング回路そのものをPPSで駆動する機能ではない。固定遅延・時刻不確かさは未測定としてUINT32_MAXを記録し、精度合格としない。
- 親機バイナリにkind10の磁気観測、kind11のセット全配置、kind12のNAV-PVT観測を追加。磁気は取得した10Hzの新規データごとに連番・読出し区間・生値・補正値・機体軸値を保存。GNSSは受信連番・受信時刻と受信機の観測iTOWを区別する。既存HEALTH/UBXは維持する。
- 親機校正セットには名称、CAD版、機体/各センサ識別名、FRDの位置/回転、既存IMU/磁気校正番号を保存。確認済み配置、個別校正の一致、PPS状態、精度未検証を別に表示する。未設定は下書きとし、CAD値を捏造しない。姿勢・位置フィルタ自体の新規実装は含まない。
- 新規TMPへCRC・同期・読戻し確認後、新規BINへ確定。失敗したTMPを復元対象外とし、以前の適用セットを維持する。最大有効番号を復元し、破損時は復元エラーを表示する。カードにセットがない場合は過去の別カードの配置を残さない。新規親機ログへセット全体を複製する。
- UIは既存「センサー校正」タブ内に追加。編集保持、明示読直し、停止中の保存、通信失効、入力検査を追加し、既存5タブ・校正機能を維持した。
- README、docs/spec.md、docs/README.md、関連ログ/通信仕様と画面設計を更新した。[操作・詳細仕様](../../docs/parent_calibration.md)。

## 検証・実測

**今回の新規ハードウェア実測はない。** 以下はソフトウェア試験とビルドの結果。

- scripts/test_host.py成功。PPS対応/失効/異常/重複/ドリフト、IMU内部時計の周回/再推定、校正セットCRC/位置/四元数、実際の保存/APIコードによる同期失敗/rename失敗/旧セット保持/破損復元/操作ガード/下書き/個別校正不一致を確認した。
- C++で生成した実構造体をPythonで読み、新しいMAG/GNSS/セットのABI・内外CRC・CSV展開を照合。実際のformatterが生成したschema3 CSVを検査し、CRCを正しく付け直した偽の時刻品質も拒否した。従来kind1〜9・旧長さログ・既存校正試験も成功。
- 既存のコンソール/Spresenseオフライン試験と新規セットUI試験が成功。下書き、編集保持、読直し、不正四元数、保存本文、保存失敗、通信失効を確認した。
- 全ネットワーク要求を模擬応答に置き換えたローカルブラウザーで375px/1280px表示を検査。横はみ出しなし、pageerrorなし。表示画像を確認した。実機画面試験ではない。
- Pico parent/child100ビルド成功。最終のサイズ・ビルド結果は生ログを参照。
- Spresense SubCore1/formatter、MainCore/loggerビルド成功。最終のプログラム表示サイズはformatter 75524byte、logger 360808byte。共有ABI版3の両コア更新が必要。

生出力・画面画像・変更前ファイル・ソース/ビルドSHA256は[raw](../raw/2026-10-25_003_parent_calibration_set/)に保存。最終ログはhost_final.txt / pico_final.txt / spresense_final.txt、画面はparent_set_375.png / parent_set_1280.png、visual_qa.json。最後の保存復元修正はparent_store_final.txt / pico_store_final.txtでも確認する。

## 未検証・更新後の確認

- 現物PPS終端、電圧・極性・GPIO波形、実機PPSとUTCの秒対応、絶対時刻差、IMU固定遅延、長期ドリフト、同時取得の実負荷、960Hzの新CSV保存負荷は未検証。過去schema2の保存安定実測を今回の版へ流用しない。
- 実機でのセット保存/読戻し/電源再投入復元、全6姿勢/磁気回転、CAD配置との一致、実機Web表示、姿勢・位置精度は未検証。
- 更新は親機とSpresense MainCore/logger・SubCore1/formatterの全て。USB個体番号と応答で識別し、記録安全停止と現物配線を確認して書き込む。親機の指定ビルドは `../P_MAIN_experiment/.venv/Scripts/pio.exe run`、Spresenseは `powershell -File scripts/build_spresense.ps1`。既存の識別付き親機書込みは `../P_MAIN_experiment/.venv/Scripts/python.exe scripts/flash.py parent`。今回はこれらの書込み操作は実行していない。
- 更新後はPPS検出→UTC対応→IMU時刻推定、PPS途絶で失効、新規ログの連番/CRC/セット参照、再投入復元を確認する。実機Webはユーザーが行い、PC Wi-Fiは切り替えない。
- GitHub Pages側で新しい操作を使うには公開画面の更新も必要。今回の変更先はこのリポジトリ内のweb/index.htmlと生成された親機内蔵画面。
