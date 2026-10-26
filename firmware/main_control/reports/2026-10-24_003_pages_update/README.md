# GitHub Pages操作画面更新（2026-10-24）

## 前提

ユーザーからGitHub Pages更新の明示指示。公開専用checkout P_MAIN_pages_publish、公開先ry32767/P_MAIN_experimentのmainを使用。元のAquaBeaconとP_MAIN_experiment作業フォルダーは変更しない。PC Wi-Fi切替・実機操作・ファーム書込みは行わない。親機温度/水圧/LCD・ハードウェア配線は対象外。

## 実施

最新web/index.htmlを公開用に適用。5タブ、Spresense状態/記録/6姿勢校正、親機磁気校正、GNSS RTK/PPK、ログ操作へ追従。全機体APIを親機http://192.168.4.1へ転送し、明示接続・role確認後に端末時刻を共有する。専用キャッシュ版を更新し、オンラインで最新取得、通信不能時は保存済み画面を使用。公開リポジトリのREADME/レポート/索引と模擬試験を追加した。

## 実測・結果

- commit: 6f3ad6464dceeb84baa1688783d9cf3b602579d0、mainへpush済み。
- [GitHub Pages公開処理](https://github.com/ry32767/P_MAIN_experiment/actions/runs/40088572296)はsuccess。
- [公開URL](https://ry32767.github.io/P_MAIN_experiment/main-control/?v=20261024)をHTTP GET、200、57589byte。取得内容は公開commitのHTMLと完全一致。5タブとSpresense操作要素を確認。
- 公開HTML/Service Worker構文チェック、専用模擬試験成功。未接続拒否、親機role確認、接続後時刻共有、API送信先、オンライン/オフラインキャッシュ、旧専用キャッシュ削除範囲を確認。
- 既存のコンソール/Spresense UI試験成功。

## 未検証

スマホからの親機実接続、ローカルネットワーク権限、実機操作、端末別キャッシュ更新。時刻/距離/IMU精度は判定しない。今回の公開更新が親機IP直結の読み込み問題を解決したとは記載しない。

## 証跡

[取得結果](../raw/2026-10-24_003_pages/verification.json)、[公開HTML](../raw/2026-10-24_003_pages/public.html)。公開側レポートはP_MAIN_pages_publish/reports/2026-10-24_001_main_control_refresh/README.md。
