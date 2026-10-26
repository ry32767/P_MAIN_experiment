# 機体の軸合わせ・カメラ時刻照合

個別センサのバイアス/倍率校正の後に行う、**組み上げた親機・子機それぞれの取付軸合わせ**。機体座標は右手系FRD（前＋X、右＋Y、下＋Z）。GNSSアンテナの位置オフセットやカメラ光学系の外部標定は、この手順から推定しない。

収録完了、解析による軸変換、SD保存、特定カメラクリップの時刻照合を別々に管理する。収録だけでは校正完了にならず、幾何的な整合性チェックの通過も絶対姿勢精度の認定ではない。

## 1. 個別校正の確認
SDから有効な校正を復元し、補正適用・参照番号一致・エラーなしを確認できれば、その個別校正は省略する。保存済みの機体軸変換も、同じセンサ・取付・個別校正番号なら再利用できる。ファイルの存在だけでは完了扱いにしない。センサ交換・取付変更・校正番号変更時は再確認/再校正する。
- 親機：Spresenseのcalibration_applied=true、磁気のenabled=true、保存/復元エラーなしを確認。個別校正を変更したら親機校正セットも保存し直す。
- 子機：BNOの校正保存成功応答を、子機SDの新規C_MAIN_BNO_CAL_*.binへ記録する。これはBNO内部DCD保存成功の履歴であり、再起動後の内部係数を読み出して一致確認したものではない。履歴がない既存DCDを未実施と断定しないが、完了証拠は不明として警告する。
- Spresenseの番号が非ゼロでもapplied=falseなら補正は未適用。最新が無効化レコードの場合、保存番号だけで有効と判断しない。

Spresenseを保存できたら通常操作の「保存済み補正を使って次へ」で進む。「校正モードを終了」も保存済み補正を維持する。補正を使わない場合だけ詳細操作を開き、無効化する確認チェックを入れて保存する。これは校正の完了操作ではなく、次回起動も補正しない設定を保存する操作である。

## 2. 機体全体の収録
「センサー校正」→「機体全体の軸合わせ・カメラ照合用収録」で親機/子機を選ぶ。

1. カメラを固定し、対象機器の通常ログとカメラ撮影を開始する。個別校正モードは終了する。親機はSpresenseの通常記録・PPS/UTC対応・IMU時計推定も必要。子機は通常IMU記録を使う。測距を停止する。
2. 「機体姿勢の収録を開始」。各区間で静止した後に「開始時刻を記録」→5秒以上保持→「終了時刻を記録」を押す。
3. 区間1は機体を水平・前向き。区間2は同じ基準姿勢から機首を上へ90度。区間3は同じ基準から右側を上へ90度。区間4は同じ基準から機尾を上へ90度。毎回基準へ戻してから指定軸だけを回し、余分に横方向へねじらない。磁気の軸合わせでは同じ場所・同じ世界基準方向を保つことが重要。
4. 区間5は30秒以上の不規則な多軸回転。画面は最初の約10秒に左右の向き変更、次の約10秒に機首の上げ下げ、最後に左右への傾きを案内する。角度・順序の厳密な一致は求めない。IMUの更新と動きの検出を表示し、30秒と動きが確認できると区間終了が可能になる。冒頭と末尾にも特徴を入れ、衝撃や周期的な繰返しを避ける。Action 6のジャイロ保存条件はEIS OFF・Wide。撮影設定はカメラ側で確認する。
5. 全区間を終了して「全区間の収録を完了」。通常ログは自動停止しない。回収前に必要な機器の記録を停止する。

収録はP_MAIN_BODY_CAPTURE_*.bin / C_MAIN_BODY_CAPTURE_*.binを排他的に新規作成する。各マーカーに機体ID、起動session、個別校正番号、単調時刻、参考UTC/時刻源、取得/欠落カウンタ、対応する通常ログ名を保存する。時刻不確かさはUINT32_MAX（未測定）。基準姿勢が正しいかはPC解析で確認するため、区間終了時の「記録済み」を姿勢判定合格とは表示しない。

保存済み軸変換が有効なら「SDの軸合わせを再利用・カメラ動作のみ」を選ぶ。4姿勢は省略できるが、区間5は毎回必要。CAMERA_CAPTURE_*.binへ開始イベント6・必要mask16で記録し、既存の軸変換を無効化しない。初回/軸変換が不一致ならBODY_CAPTUREの4姿勢＋動作を行う。

動作確認はゆるい収録確認であり、各方向を厳密に再現する試験ではない。有限なジャイロのどれかの軸で0.08rad/sを超える動きを、1秒以上離れた時刻で検出する。特定の角度・3軸すべての検出は必須にしない。開始直後3秒は旧応答を除外し、最新更新が5秒以上途絶えたままでは区間終了しない。これらは同期精度や相関成功の認定値ではない。

計測中の記録停止・SD異常・個別校正番号の変化・15分超過で中止扱いにし、既存ファイルは残す。親機は両側のGPS/PPS UTC・Spresense記録が成立した時だけ開始し、時刻条件が失われても中止する。RTCや端末時刻だけでは開始しない。明示中止も中止マーカーを残す。校正中のファイル新規切替・IMU再初期化・個別校正変更は拒否する。中断は解析側の時刻・連番・欠落条件でも確認する。

## 3. 収録データから軸変換を計算
親機はBODY_CAPTURE、対応する親機HEALTH_RANGE、Spresense Ixxxxx.CSVを回収する。Spresense CSVはparent_sessionとPPS UTCで同じ収録を選ぶ。子機はBODY_CAPTUREと対応するC_MAIN_IMU_100Hz_*.binを回収する。

```powershell
../P_MAIN_experiment/.venv/Scripts/python.exe scripts/body_alignment.py --body P_MAIN_BODY_CAPTURE_00001.bin --imu I00001.CSV --mag P_MAIN_HEALTH_RANGE_00001.bin --out parent_axes.json --gyro-out parent_movement.csv
../P_MAIN_experiment/.venv/Scripts/python.exe scripts/body_alignment.py --body C_MAIN_BODY_CAPTURE_00001.bin --imu C_MAIN_IMU_100Hz_00000.bin --out child_axes.json --gyro-out child_movement.csv
```

ファイル名は例。実際に収録へ記録されたファイルを選ぶ。入力ファイルや既存出力を上書きしない。Python標準ライブラリのみ使用する。

- IMU：水平時の重力方向と機首上げで前/下方向を求め、右方向を外積で定める。4姿勢での方向整合性（5度ゲート）、静止・分散・有限値・点数を確認する。
- 磁気：既知の機体回転に対して世界座標で同じ磁場へ戻る取付回転を、複数の初期姿勢から最適化する。磁場ノルム整合性、相対残差5%ゲート、4番目の姿勢を含む整合性を検査する。完全なハード/ソフトアイアン校正を代替しない。姿勢を正しく再現できない場合は拒否される。
- 校正番号の混在、未完了/中止の収録、CRC異常、区間内の欠落増加、別起動のログを拒否する。親機のSpresenseとの区間対応にはGNSS/PPS UTCを要求し、RTCや端末時刻を代用しない。

上記の数値は実装の受入ゲートで、本機の精度測定値ではない。

## 4. カメラ時刻を照合する場合
カメラの動作収録は撮影/実験ごとに必須で、省略できない。通常ログの新規作成/記録準備操作・再起動・SD再認識では完了状態を解除し、今回の起動・ログ名・個別校正番号に一致する収録だけを使う。過去のカメラ時刻変換やSD保存履歴では代用しない。カメラの撮影切替は本機から自動検出できないため、新しいクリップを開始する時は新しい実験の準備操作から行う。

カメラ専用収録は、4姿勢を再計算せずに時刻照合用CSVを取り出せる。

```powershell
../P_MAIN_experiment/.venv/Scripts/python.exe scripts/body_alignment.py --movement-only --body P_MAIN_CAMERA_CAPTURE_00002.bin --imu I00002.CSV --out parent_camera_capture.json --gyro-out parent_movement.csv
../P_MAIN_experiment/.venv/Scripts/python.exe scripts/sync_camera_imu.py --capture-info parent_camera_capture.json --camera camera_gyro.csv --target parent_movement.csv --out parent_camera_time.json
```

子機では--bodyへC_MAIN_CAMERA_CAPTURE、--imuへ対応する子機IMU binを渡す。出力は対象クリップの変換で、軸校正の再保存は不要。capture-infoと動作CSVのSHA256を照合する。既存軸JSONへ追記する場合は--solutionも指定できるが、機体IDと個別校正番号の一致が必要。

Action 6の未編集動画から、映像との対応を保ったジャイロCSVを別途用意する。CSVはtime_s,gx,gy,gz、またはtime_s,gyro_magnitude。time_sは対象映像に対する秒で、単位と開始点を確認する。**MP4を直接読み出す機能は今回含まない。**

```powershell
../P_MAIN_experiment/.venv/Scripts/python.exe scripts/sync_camera_imu.py --camera camera_gyro.csv --target parent_movement.csv --solution parent_axes.json --out parent_axes_camera.json
```

機体IMUとカメラIMUの回転速度の大きさを照合するので、この時刻照合自体にはカメラIMU軸の事前一致を必要としない。20秒以上の動きから冒頭/末尾の窓で時刻差と倍率（時計のずれ）を推定し、中間窓で整合性を確認する。静止・繰返しによる曖昧な一致、データ欠落、弱い相関、範囲外の倍率は拒否する。対象クリップのCSVハッシュをJSONへ記録する。

対応式は target_us = camera_scale × camera_time_s × 1000000 + camera_offset_us。親機のtargetはPPS対応UTC、子機は取得単調時刻。子機の取得遅延やセンサ内部遅延、カメラ内IMUと映像の既存対応は未測定で、相関成功を絶対同期精度の保証にしない。親子別々の機体の動きを直接照合せず、それぞれに固定されたカメラと組にする。

時刻変換はそのクリップ・収録のみに有効で、次の撮影/再起動へ流用しない。カメラに時計を設定する機能や撮影を開始する機能ではない。

## 5. 結果を保存・使用する
対象機器の記録と測距を停止し、画面で解析結果JSONを選んで「解析結果をSDへ新規保存」。対象のSDにある収録全レコードのCRC・完了状態、機体ID、起動session、現在の個別校正番号、正規化四元数等を検査する。別の機体や未完了収録の結果は使わない。

P_MAIN_BODY_ALIGN_*.bin / C_MAIN_BODY_ALIGN_*.binへTMP同期・読戻し確認・未使用BINへの確定で保存する。起動/SD再認識でCRCが正しい最大番号を復元。失敗TMPは復元せず、以前の値とログを残す。個別校正をやり直すか、新しい4姿勢のBODY_CAPTUREを始めると、結果を保存し直すまで未完了として警告する。CAMERA_CAPTUREだけなら保存済み軸は再利用する。

通常ログの生値/従来列は保持し、各通常ログで校正番号の一致とセンサ状態を確認できた時にBODY_PROFILE（kind14）を一度複製する。起動直後の状態待ちによりファイル先頭になるとは限らない。親機磁気の機体軸列は、対応するBODY_ALIGNがあれば保存回転を利用する。その他のベクトルの後処理には以下を使う。

```powershell
../P_MAIN_experiment/.venv/Scripts/python.exe scripts/decode_binary.py C_MAIN_IMU_100Hz_00000.bin --csv-dir decoded
../P_MAIN_experiment/.venv/Scripts/python.exe scripts/apply_body_alignment.py --solution child_axes.json --input decoded/imu.csv --out child_body_vectors.csv
```

実際のCSV出力名に合わせる。ax/ay/az、gx/gy/gz、mx/my/mzまたは補正列からbody_*列を追加する。Game Rotation Vectorの世界座標や磁北基準そのものは変更しない。使用する機体・個別校正・取付状態が一致する結果を選ぶ。

## 実験開始前の警告
実験の新規開始/再開と測距開始では、両機の新しい状態を照会する。個別校正または保存済み機体軸が未完了/不一致/不明なら、記録停止・新ファイル作成の前に警告を表示。「校正へ戻る」/Escで中止し、明示的な「未校正を承知して開始」でのみ続行する。通常状態でもサーバー側で開始時に再確認し、警告を承知していない状態変化を自動的に迂回しない。

記録の開始/再開は撮影・動作収録の準備であり、この時点では測距を許可しない。カメラ動作収録の完了kが親子とも成立するまで、測距開始・テスト・明示承知API・USB Tと実際の送受信を本体側でも拒否する。個別/軸校正の警告を承知してもカメラ収録は省略できない。撮影後のPC時刻照合そのものは、実験前には要求しない。

姿勢校正完了rと、今回のカメラ動作収録完了k、過去のクリップの時刻変換tは別々に表示する。電源投入時の従来の自動ロギングとUSB sによるSD復旧は準備記録として扱う。CLIの--allow-uncalibratedもカメラ動作収録を省略する指定にはならない。

## 通信・保存の互換性
RS485の版5/163byteは維持し、CONTROL 17=BODY_BEGIN、18=BODY_MARK、19=BODY_STATUS、20=BODY_END、21=BODY_IMPORTを追加。BODY_MARK argは区間1〜5、bit8が終了。BODY_END arg0が完了、1が中止。BODY_IMPORTは68byteの解析結果。旧子機は未対応として扱い、完了を推測しない。短い状態JSONのa/m/p/c/g/v/t/r/bは、収録中/区間mask/現在区間/収録番号/保存番号/軸mask/時刻変換あり/個別校正と軸一致/BNO保存履歴番号を表す。

BODY_BEGIN arg0=4姿勢＋動作、arg1=保存軸を再利用して動作のみ。状態JSONにk=今回の動作完了、o=必要mask31/16、d=検出したセンサ軸mask、s=区間5経過秒（最大900）、q=動作更新0不明/1更新中/2終了条件確認を追加。最大101byteでCONTROL data104byteに収める。Health.flags bit14 CAMERA_READYは今回の動作収録完了を通知し、旧子機の未設定を未完了として扱う。

SpresenseのPPS診断Nav.fixを0xD0|gyro軸maskとし、タグがある場合だけ動作ヒントとして読む。位置のあるNavや旧版fix=0は解釈しない。ABH3版3/128byteとSD/PPS/校正診断は維持する。

RECORD_START/NEWの未完了警告はerror10、明示続行arg=0x43414C21。これは精度合格を意味せず、未校正の開始を承知したことだけを表す。動作中の収録を壊す操作は承知しても拒否する。
