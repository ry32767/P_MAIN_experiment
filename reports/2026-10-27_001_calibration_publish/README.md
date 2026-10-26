# 校正機能・統合ソースと公開操作画面の更新

| 項目 | 内容 |
|---|---|
| 実施日 | 2026-10-27 JST |
| 目的 | 現行の親子校正・カメラ用動作収録をGitHubとPagesへ反映 |
| 公開先 | ry32767/P_MAIN_experiment の main |
| 対象 | firmware/main_control、web/main-control |
| 更新commit | bdc291b1059386f8fd40e485551841f9c97f5085 |

## 前提

作業元はAquaBeacon_MAIN_firmware。専用の公開用チェックアウトを使用し、旧Spresense単体実験のソースは変更しない。実機の再書込み・停止・SD操作・PCのWi-Fi切替は行わない。今回の検証はPC上の模擬試験・ビルドと公開サーバーへのHTTP照合で、機器接続・給電・配線は対象外。実機配線の直近の確認は統合版の2026-10-26の配備・給電後レポートを参照。

## 変更

- 最新の親子統合ソース、Spresense両コア、PC解析・検証ツール、操作仕様と過去の検証要約を同期。
- 校正の順序、保存済み補正・軸合わせの再利用、毎回必須のカメラ用動作収録、未校正警告、補正無効化の確認操作を公開画面へ反映。
- 公開画面は接続操作とP_MAIN応答を確認して端末時刻共有を開始する。機体APIは端末から192.168.4.1へ直接通信する。別ホストへのAPI要求を拒否し、ログをGitHubへ送信しない。
- オンラインでは新しい画面を取得し、オフライン保存も更新。キャッシュ名はaquabeacon-main-control-20261027-v10。
- 公開版はPlatformIOの作業環境固有core_dirを除去。APパスワードは非公開のinclude/secrets.hで設定する。設定例は仮値のみ。生ログ、SD実データ、実測座標、無線認証情報、ビルド出力は含めない。
- 旧単体実験と現行統合版の配線が混同されないよう、READMEに構成の区切りと現行仕様へのリンクを追加。

## 検証結果

- 公開コピーの`python scripts/test_host.py`：終了コード0、PASS表示は43件。通信・CRC・SD復元・軸保存・カメラ収録必須判定・Web模擬試験を含む。
- Pico `parent` / `child100`：ともにSUCCESS。
- Spresense formatter / logger：Sony公式コア3.4.7でともにビルド成功。
- `node scripts/test_main_control_pages.cjs`：PASS。5タブ・校正操作・JS構文・接続/機器役割/送信先ガード・接続後のみ端末時刻共有・オフラインキャッシュ更新を確認。
- 非公開の実行出力は`reports/raw/2026-10-27_001_calibration_publish/`へ保存。
- [Pagesデプロイ](https://github.com/ry32767/P_MAIN_experiment/actions/runs/40355133862)：success、対象commit `bdc291b1059386f8fd40e485551841f9c97f5085`。
- [公開画面](https://ry32767.github.io/P_MAIN_experiment/main-control/)：index.htmlとsw.jsがHTTP 200。配信バイト列を対象commitのGit blobと照合し、両方一致。ハッシュ・サイズは[evidence/deployment.json](evidence/deployment.json)へ保存。
- 公開後の確認はHTTP取得のみ。スマホから実機への操作は未検証。

PC上のテスト結果を実測精度と扱わない。

## 未検証

実機Webからの一連の操作、カメラ動画のIMU抽出・実データ照合、機体軸の実測精度、UTC絶対精度、距離精度、長時間安定性は今回の公開作業で検証しない。子機の取付傾き補正はPC解析用の軸変換で、リアルタイム姿勢出力への適用は未実装。
