# 親子実験画面のGitHub Pages公開

| 項目 | 内容 |
|---|---|
| 実施日 | 2026-09-20 JST |
| 目的 | 親子統合ファームの操作画面を既存公開Pagesへ追加 |
| 対象 | web/main-control、README、Pages入口 |
| 判定 | 静的構文・内容検査合格。GitHub Pages配信HTTP 200確認済み |

## 前提条件
既存リポジトリのSpresense取得実験とは独立した、親子統合ファーム用の画面。旧ファームウェア・ピン配置は変更しない。実機Wi-Fi切替はユーザー指示で実施しない。ユーザーのUSB再接続後、親機個体・役割を確認しPages用通信許可を追加したファームの書込みに成功。

## 変更
- 公開画面: https://ry32767.github.io/P_MAIN_experiment/main-control/
- ページは親機192.168.4.1へ直接通信。明示的な接続ボタンとローカルネットワーク許可の案内を追加。
- オフライン用画面キャッシュ、機体内操作画面への代替リンク。
- READMEと既存Pagesの入口から新画面へのリンク。
- ログ・Wi-Fiパスワード・非公開基板設計は公開しない。

## 検証
UI JavaScript、Service Workerのnode --check成功。API呼出しが親機アドレスを使うこと、機体接続前は操作を始めないことを静的確認。公開画面を開いただけでは実機への通信を開始しない。

## 未検証
実機Wi-Fi上でのブラウザ許可・Pagesからの操作・オフライン再読込みは未実測。親機にPagesオリジン用の通信許可が必要。対応しないブラウザや未更新機体では機体内画面を使用する。インターネット経由で機体を遠隔操作する機能ではない。

ブラウザ仕様の根拠: [Chrome公式のローカルネットワークアクセス説明](https://developer.chrome.com/blog/local-network-access?hl=ja)。

## 公開結果
[Pages公開ワークフロー](https://github.com/ry32767/P_MAIN_experiment/actions/runs/35490848602) 成功。公開HTMLとService Workerの取得、接続ボタンとAPI接続処理の配信を確認。公開対象commit: 859e52431f736e94e7ae27b56c80bd557eba16ed。
