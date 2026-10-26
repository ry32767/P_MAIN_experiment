# 172.20.10.4へ接続できない：テザリング状態の調査

## 前提
- ユーザー報告：172.20.10.4で親機へ接続できなくなった。
- PCのWi-Fiは変更しない。実機Web画面はユーザーが確認。SDフォーマット・ログ削除・実機書込みなし。

## 実施
- USB番号A444A41EE9334705のCOM6とh応答P_MAINを照合し、h/Wを読取り。
- Wi-Fi保存設定・再接続処理と直前のGNSS変更範囲を確認。直前のGNSS変更ではWi-Fi接続・設定保存・Web待受コードを変更していない。
- iPhoneのインターネット共有許可・互換性優先・設定画面保持の確認を依頼し、USBで状態変化を観測。

## 初回実測
- 親機AP起動true、192.168.4.1、Web待受true、直接AP接続1台。Webのページ完送2回、状態取得149回。
- wifi_configured=true、sta_connected=false、sta_status=6、STA IP未設定。保存済み接続情報はあり、接続試行は20回（観測中21回へ増加）。
- この状態では172.20.10.4へのWeb通信はできない。Webサーバーの停止や、別のIPへ接続済みという状態とは区別する。
- 親機SDはNONE、PPK記録中、ファイル00051。RAWX/SFRBX/UBX byteの保存件数が増加し、現時点の保存欠落0。停止・再起動していない。
- Web HTTP累計タイムアウト4はあるが、STA未接続の原因とはこの値だけで断定しない。

## 未検証
- スマホの共有許可・周波数/互換性、接続先名/パスワード変更の有無、接続数制限。
- テザリング再接続後のIPと、ユーザー端末からのWeb到達。現時点で復旧済みとはしない。

## 証跡
reports/raw/2026-10-25_010_tether_connection/initial_status.txt/json、monitor_status.txt/json、network_sources.json。

案内根拠：[Apple公式・インターネット共有に接続する](https://support.apple.com/ja-jp/111785)。接続完了までは設定画面を表示したまま待つこと、必要時に互換性を優先を使うことを確認。


## 共有ON確認後の追加調査と親機更新
- ユーザーが共有ON・設定画面を開いていると回答。スマホの共有OFFを原因と決めつけず、親機に無線チップ診断を追加した。
- USB WにCYW43 link status、join state、最終接続試行からの経過msを追加。`@NET STATUS`でパスワードを含まない設定公開項目と診断、`@NET REJOIN`で既存RAM設定による再接続予約を追加。
- 再接続処理そのもの・保存済み接続情報・基準局は変更していない。USBから読み取ったSSIDは元の保存設定で、saved=true、dirty=false、boot_checked=true、パスワード登録ありを確認。
- README.mdとdocs/spec.mdを同時更新。新規docsなし。
- 親機/子機100Hzの既定ビルド成功、scripts/test_host.py全体が終了コード0。Wi-Fi切替型テストは実行していない。
- scripts/flash.pyでUSB番号とP_MAIN応答を再照合し、Xqで記録停止・同期を確認して更新。既存00051ファイルを削除せず、新しいセッションで再開。

## 復旧実測
- 再起動後、保存済み設定を自動読込み、接続試行1回でsta_connected=true、sta_status=3、STA IP=172.20.10.4をUSBで確認。AP起動・Web待受もtrue。
- 接続前はCYW43 link_status=1/join_state=1、接続後も1/1となる実測があり、この低水準の値だけでIP取得済みとは判定しない。WiFi.connectedと実際のSTA IPを併用する。
- 再起動で接続が復帰したが、再起動前の認証/電波/内部状態のどれが原因だったかは未確定。保存情報の不一致や共有OFFと断定しない。
- スマホで http://172.20.10.4/ が表示できるかユーザーに確認を依頼。USBでの接続成立と実機Web到達は区別する。
- 生ログ：driver_status.txt/json、recovered_status.txt/json、build.txt、host.txt、flash.txt。設定公開項目を含むUSB生ログはraw内に保持。


## 復旧後の追加観測
- 30秒の連続USB観測31件すべてでテザリング接続true、IP=172.20.10.4、接続試行1回を維持。
- SD reason=NONE、PPK recording=true、新ファイルP_MAIN_GNSS_00052.ubx、RAWX113/SFRBX56、保存欠落0を末尾で確認。既存実験ファイルは保持。
- この区間のWeb要求0で、ユーザー端末からの画面表示成功はまだ確認していない。長時間安定性や根本原因の確定とは区別する。


## ユーザーによる最終確認
- ユーザーがテザリング元スマホから http://172.20.10.4/ の親機画面が「表示されました」と回答。
- USBの接続/IP確認に加え、ユーザー端末からの実機Web到達を確認し、今回の接続不能は復旧とする。
- 根本原因の確定と長時間安定性は未検証のまま。PC Wi-Fi切替は行っていない。
