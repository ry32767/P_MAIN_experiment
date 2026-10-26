# GPS L5を実配信する基準局の再確認

## 前提
- 2026-10-25、学園都市・長田高校周辺で使う候補のうち、GPS L5を配信する局を求められた。
- Galileo E5aだけを確認した近場の局と、GPS L5の確認済み局を区別する。
- 先行調査の8局からGPS L5のあった2局を今回あらためて取得した。全国・全サービスを網羅した最寄り保証ではない。
- 現行AGENTS.mdに従い、PC Wi-Fiの切替、実機Web操作、SD操作、書込み、ピン/ファームウェア変更を行わない。

## 実施
- ntrip1.bizstation.jp:2101へ、NTRIP v1のGETを認証なしで送り、それぞれ短時間取得。
- CRC24Qを検証し、MSM5の信号IDと観測セルを解析。
- GPS 1075のID23はL5Q、QZSS 1115のID23もL5Q、Galileo 1095のID23はE5aQ。RTKLIBの公開一次ソースに照合。
- 信号マスクだけでなく、搬送波が無効値でなくCNRが正である観測セルを確認した。
- 距離は放送1005の座標から、概略代表点（学園都市34.68,135.06、長田高校周辺34.67,135.14）までPythonで算出。精密な校舎/駅入口の座標による距離ではない。

## 実測結果
| 配信先名 | サーバー | ポート | 学園都市周辺から | 長田高校周辺から | 無認証受信 | 確認信号 |
|---|---|---|---|---|---|---|
| `3C66E6DA` | `ntrip1.bizstation.jp` | 2101 | 約39.4 km | 約41.8 km | ICY 200 OK | GPS L1/L5・QZSS L1/L5・Galileo E1/E5a |

3C66E6DA：GPS L5Qの有効搬送波/正CNR観測は80セル、CRC不一致0。

| `3C67E4E6` | `ntrip1.bizstation.jp` | 2101 | 約48.4 km | 約42.5 km | ICY 200 OK | GPS L1/L5・QZSS L1/L5・Galileo E1/E5a |

3C67E4E6：GPS L5Qの有効搬送波/正CNR観測は80セル、CRC不一致0。


- 2局とも認証ヘッダーなしで取得できた。ユーザー名・パスワードは空欄で接続できることをPCで確認。
- Drogger運営元は当面無料の運用と、一般NTRIP Clientの利用に制限がないことを案内している。
- この2局は近場の3C722D56やbtf_nishiyamacho3より遠い。GPS L5の共通観測を増やせる可能性と、基線が長くなる影響を現地で比較する必要がある。
- 学園都市では距離の近い3C66E6DAから、長田高校ではほぼ同距離なので2局を比較する案。いずれもFIXや精度の保証ではない。

## 未検証
- 親機による両局への接続、受信機の補正採用、GPS L5の実追尾、RTK FIX成立、測位精度。
- 各局の長期稼働、基準局座標の品質/基準系、設置環境。
- 調査済みの近場2局ではGPS L5を確認していない。全近隣局がGPS L5非対応と断定したわけではない。

## 証跡
`reports/raw/2026-10-25_010_l5_stations/` の `probe_l5.py`、`streams.json`、各 `.response`。
先行の8局比較は `reports/2026-10-25_004_rtk_candidates/README.md`。

## 一次資料
- [Drogger運営元：料金・接続条件](https://drogger.hatenadiary.jp/entry/ntripCaster)
- [RTKLIB：MSM信号ID表・デコード](https://github.com/tomojitakasu/RTKLIB/blob/master/src/rtcm3.c)
