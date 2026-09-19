# 配線と設計根拠

参照元は隣接するAquaBeacon作業ツリー（2026-09-19）。HEAD=`ca4011486411e855d41e3264d2c4b8c85e0ad196`。
J_SPR1の実配線は、**未コミット更新を含む** `pcb/BRINGUP_BOARDS.md` と `pcb/PARENT_MAIN/部品一覧.md` を採用しました。元リポジトリの内容は変更していません。

| 元ファイル | SHA-256 |
|---|---|
| pcb/build_parent_main.py | 26DEA94D42EC0378685536A8330329A97753F0C09D8B50BA01C510C7348D3129 |
| pcb/build_parent_pwr.py | 8652893CDF92D648E12C2DF973082527B5A4BC4B60F79F3EEEDF51905DBCDDB0 |
| pcb/BRINGUP_BOARDS.md | 0D784B974CE6C5DE6C8FD6ABE970D3E1472561388A742D4F238B7F67D40D45DD |
| pcb/PARENT_MAIN/部品一覧.md | E6FC9D678123B982CE79BCDEA225B24B8951C2D8C5525D4F254B2A31D10DFC51 |

生成スクリプト表記はP-MAIN v5.9、P-PWR v1.5。v5.8/v1.4からの主要変更は図面レイアウトです。実物のシルクも記録してください。

| 機能 | Pico GPIO | 物理ピン | この実験での用途 |
|---|---|---|---|
| 状態LED | GP2 | 4 | PPS対応候補で点灯、未確定で点滅 |
| INA226 ALERT | GP3 | 5 | 入力、状態記録。アラートしきい値は設定しない |
| I²C SDA/SCL | GP4/GP5 | 6/7 | P-PWRのINA226（0x40） |
| PPS | GP6 | 9 | J_SPR1.1、100Ω直列 |
| Spresense UART RX | GP7 | 10 | J_SPR1.2、100Ω直列、PIO、TXなし |
| 外付けGNSS UART | GP8/GP9 | 11/12 | 不使用・初期化しない |
| SD SCK/MOSI/MISO/CS | GP10/11/12/13 | 14/15/16/17 | SPI1、1 MHz |
| SDカード検出 | GP15 | 20 | Low=挿入。元資料の実測はC-MAIN、P-MAIN実物でも確認 |
| TX PWM | GP16 | 21 | Low固定 |
| ボタンラダー | GP26 | 31 | 起動時V3Dの有無を受動確認。未押下約3.3V |
| TX温度ADC | GP27 | 32 | 今回は未使用 |
| LCDバックライト | GP28 | 34 | 不使用・初期化しない |
| テザーDATA | GP0/GP1 | 1/2 | 不使用・初期化しない |

P-PWRのシャントは20 mΩ。INA226はBus Voltageレジスター×1.25 mV、符号付きShunt Voltage×2.5 µVを使い、電流=シャント電圧÷0.020Ω。校正レジスター不要の生値から算出しています。実測にはシャント誤差と測定誤差が含まれます。0xFEのメーカーID=0x5449も確認します。

INA226は物理的な電流遮断器ではありません。このファームは低電圧・過電流による電源遮断を実装していません。基板のヒューズ等が保護を担当します。

SpresenseはCXD5602PWBMAIN1、拡張CXD5602PWBEXT1、SonyマルチIMU CXD5602PWBIMU系を想定。内蔵IMUはありません。別のIMU型番の場合はドライバーを変更する必要があります。IMU SPI5はSony公式ボード初期化関数で設定し、SDは拡張ボードのSDHCIを使います。

一次資料：

- [Sony GNSS API](https://developer.spresense.sony-semicon.com/spresense-api-references-arduino/classSpGnss)：`start1PPS`、測位周期、UTC。
- [Sony Arduino GNSS実装](https://github.com/sonydevworld/spresense-arduino-compatible/tree/v3.4.7/Arduino15/packages/SPRESENSE/hardware/spresense/1.0.0/libraries/GNSS)：`Serial2`とは別のGNSSハードウェアPPS。
- [Sony SDK Multi-IMU公式サンプル](https://github.com/sonydevworld/spresense/tree/master/examples/cxd5602pwbimu)：レート120 Hz、加速度レンジ4、角速度レンジ500のドライバー設定。
- [Sony拡張ボード](https://developer.sony.com/ja/spresense/products/spresense-extension-board/)：レベル変換・ジャンパー設定。
- [TI INA226](https://www.ti.com/lit/ds/symlink/ina226.pdf)：レジスター分解能・シャント電流換算。
- [Arduino-Pico](https://github.com/earlephilhower/arduino-pico)：PIO UART、マルチコア、Wi-Fi、SdFat。
