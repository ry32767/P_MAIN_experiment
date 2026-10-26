# 親機コンソール復旧とSpresense SD形式確認

## 前提
- ユーザー報告：親機 http://192.168.4.1/ を開けない。Spresenseカードを再フォーマット、exFAT、空き容量60GB（容量はユーザー申告）。
- PC Wi-Fiは切り替えない。SDフォーマット・既存ログ削除は実施しない。親機温度・水圧・LCDは対象外。
- 書込み前に親機USB A444A41EE9334705とP_MAIN応答、Spresense USB 10878C89E6DBED1197F5F12D62C613ACとロガー応答を照合し、安全停止した。

## 実施
- Arduino-Pico実装を確認。beginNoBlockはAPを残して内部modeをWIFI_STAにするため、外部Wi-Fi接続の開始後にWIFI_AP_STAを設定し直す。設定を空にした場合はSTAを切断しAPへ戻す。
- Web USB診断へ画面要求/完送、ping、状態取得の累計とAPモード有効フラグを追加。URL・本文・秘密を出さない。
- Spresense USB vへ読み取り専用のカード先頭/パーティション情報診断を追加。既存の空き容量診断も維持。自動フォーマットは行わない。
- 親機通常版とSpresense MainCoreを更新。SubCore ABIは変更なし（ビルドは両コア確認）。

## 実測・確認結果
- 修正前：AP起動true、IP 192.168.4.1、接続1、mode=1を確認。モード管理の不整合は実在するが、ブラウザー障害の唯一の原因だったとは断定しない。
- 更新後：ユーザーがpingと親機画面の両方が開くことを確認。
- 最終USB：AP起動true、mode=2、接続1、画面要求2/完送2、ping1、状態取得64、完送78、期限切れ0、最終HTTP200。
- Spresenseカード：MBR署名aa55、partition_type=07、開始LBA2048、OEM=EXFATを読み取り確認。MOUNT errno=2、保存行0。現在の失敗は空き容量不足ではなく未対応形式と整合する。
- Sony公式SDK資料はexFAT非対応と明記： https://developer.sony.com/spresense/development-guides/sdk_tutorials_ja 。導入済SDK3.4.7もFAT有効。
- Spresense STOPPED_REMOVE_SDを確認し、カード交換のため停止状態を維持。最終親機からもSTOPPED、recording=falseを確認。親機記録はsd_reason=NONEで継続。
- 最終Spresense PPS対応付け/IMU時計準備はtrue。時刻精度の実測合格とはしない。
- 親機/子機100ビルド、Spresense両コアビルド、ホスト試験を実施。Wi-Fiモード試験はライブラリ同様にbeginNoBlockがSTAへ変えるスタブで確認。

## 未検証・残作業
- FAT32カードの装着と保存再開、CSV読戻しによる全サンプル記録の確認が必要。現在のexFATでは保存再開できない。
- 外部Wi-Fi接続中の実機AP+STA動作、長時間ブラウザー操作、物理的な校正・同期精度は未検証。
- SDフォーマット・既存ログ削除、PC Wi-Fi切替、親機温度/水圧/LCD検証は行っていない。

生ログ・ビルド・書込み結果は reports/raw/2026-10-25_005_console_sd/ に保存。

## 後続の再開確認
ユーザーによるFAT32への変更後、記録再開と保存行数増加を確認。[SD記録再開の実測記録](../2026-10-25_006_spresense_sd_resume/README.md)を参照。
