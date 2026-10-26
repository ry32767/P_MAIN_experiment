# テザリング元スマホの親機コンソール・RTK設定SD保存

## 前提
- ユーザー報告：テザリングを提供しているスマホから親機Web画面を開けない。RTK関連設定をSDへ保存できない。
- PCのWi-Fi変更、test_web_device.py/test_web_controls.pyの実行、SDフォーマット・ログ削除は禁止。親機温度・水圧・LCD、ピン割当ての変更は対象外。
- 作業開始時のUSB読取りで、USB番号A444A41EE9334705のCOM6とrole=P_MAINを照合。Web APは192.168.4.1、mode=3、サーバー待受あり。RTKのwifi_connectedはfalseだった。

## 実施
- Web Origin許可先が192.168.4.1に固定され、テザリング側IPからのPOSTが拒否される処理を修正。実際のAP IP・接続中のSTA IP・既存GitHub Pagesだけを許可する。
- テザリング元スマホで使うSTA IPを時刻・位置タブとUSB W診断へ追加。APアドレス192.168.4.1を明示設定。
- RTK保存は従来のconfig→saveの二要求から、configのsave=1による一要求へ変更。SD書込み・同期・読戻しをWi-Fi再接続より先に行う。
- Web保存ボタンで測距停止→親機記録の同期・停止完了→設定保存とする。停止失敗時は設定要求を送らない。保存後の記録再開は手動。
- 同じWi-Fi情報の再適用による再接続を抑止し、Wi-Fi情報変更の適用はHTTP応答送信後へ延期。二重スロット・CRC・旧設定への復帰・秘密の非公開・既存ログ保持を維持。
- README.mdとdocs/spec.mdを更新。新規docsファイルなし。

## 実測・確認
- 親機・子機100Hzの通常ビルド成功。
- scripts/test_host.pyは依存入りの../P_MAIN_experiment/.venv/Scripts/python.exeで完走（終了コード0）。通常pythonにはziglangがなく、その失敗と区別した。
- 実際のGNSS APIハンドラーで、単一要求保存・CRC読戻し、停止中のみ保存、短い書込みの失敗と旧スロット保持、同一Wi-Fiの再接続抑止を試験。
- 実際のwebTickとOrigin関数を用いたホスト試験で、AP/STA許可、外部Origin拒否、STA未接続/0.0.0.0拒否、応答完送前にWi-Fi変更しないことを確認。
- 画面のオフライン試験で停止完了後の一要求保存、停止失敗時の中断、秘密欄クリア、保存設定復帰を確認。
- 初回書込みはCOM6がPermissionError(13)で開けず中断。再試行時にUSB番号とP_MAIN応答を再照合し、X/qによる記録停止後に書込み成功。
- 初回更新後の診断で、softAPConfigをAPモード設定前に呼ぶとSTAの固定IP設定になることを確認して修正。最終版はWiFi.mode(WIFI_AP)→softAPConfig→softAPの順。親機再ビルドとwebTick試験を確認し、識別・安全停止を再度行って最終版を書込み成功。
- 最終USB確認：role=P_MAIN、sd_reason=NONE、AP起動true、IP=192.168.4.1、mode=2、待受true、テザリング未接続、STA IP未設定。新しいnetwork/sta_ip診断項目で更新反映を確認。親機SDの記録再開を確認したが、設定保存そのものの実機検証とは区別する。

## 未検証
- テザリング元スマホからSTA IPでの画面表示・保存操作、実機SDへの設定保存・再起動保持。スマホ側の接続端末への通信制限も未確認。
- AP+STA接続時の長時間通信・チャネル変更後の直接AP再接続、RTK成立、時刻・距離・位置精度。ホスト試験を実機合格とは扱わない。

## ユーザー確認手順
1. 更新後にAquaBeacon-MAINへ接続して http://192.168.4.1/ を再読み込みする。
2. 時刻・位置タブにWi-Fi情報を入力し「親機の記録・測距を停止してSDへ保存」。保存・読戻し確認済み表示を確認する。必要な記録再開は手動。
3. テザリング元のスマホでは、同タブまたはUSB Wのsta_ipに表示する http://<STA IP>/ を開く。192.168.4.1は親機APへの直接接続用。
4. 記録を安全停止した後の再起動で保存値が復帰するか確認する。SDを抜く場合は必ず安全停止・電源断後に行う。

生ログ・変更前ソース・ホスト試験・ビルド・書込み試行結果：reports/raw/2026-10-25_006_tether_rtk_save/。
導入済Arduino-PicoのWiFiClass.cpp/WiFiAPSTA.inoを根拠に実装を確認。公開ドキュメント https://arduino-pico.readthedocs.io/en/latest/wifi.html はAP+STA非対応との記載もあり、導入済ソースとの差異があるため公開記載だけで動作保証しない。
