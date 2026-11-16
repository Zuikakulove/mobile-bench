# フェーズ 1(固定時間方式への移行)実装計画

- 作成日: 2026-11-16
- 土台: ブランチ `claude/determined-sagan-sxn4wu`(PR #4)の先頭のコミット `5385406`。ファイルの行番号は、その版のものである。
- **節の呼び方**: 「§5.1」のように § を付けたものは、土台の版の `docs/DESIGN.md` の節である。本書の節は、「P3.2」のように P を付けて呼ぶ。
- 前提: PR #4 が `main` にマージされていること。マージされるまで、P4 の PR には着手しない。
- 位置づけ: `docs/DESIGN.md` §8 フェーズ 1 の作業を、PR 単位の手順にしたもの。実装は別のセッションが行う。各 PR に渡すプロンプトは P4 にある。
- 本書の作成では、リポジトリのコードを変更していない。P3.21 の試作は作業用の場所で行い、リポジトリには入れていない。
- 計画と違う実装にした点は、その PR の説明に書き、本書の P7 に 1 行足す。

主張の確からしさは、`docs/DESIGN.md` §0 と同じ印(【コード】【実行】【実機】【資料】【推測】)で示す。本書の【実行】は、2026-11-16 にこの計画の作成で確かめたものを指す(出典は付録)。

本書の節:

- P0 依頼者の判断が要る点(最初に読む)
- P1 全体像(PR の一覧、項目の対応表、入れないもの、フェーズ 0 の残り)
- P2 共通の約束(全 PR)
- P3 設計の詳細(この計画で決めたこと)
- P4 PR ごとの手順とプロンプト
- P5 実機の確認の手順(依頼者向け)
- P6 リスクと対処
- P7 実装時の変更の記録

---

## P0. 依頼者の判断が要る点

本書の PR がマージされた時点で、P0.1 の「推奨」と P0.3 の一覧を承認したものとして、実装のセッションは進める。推奨と違う案を選ぶ場合は、マージの前に本書を直すか、プロンプトを渡すときにそう指示する。

### P0.1 着手の前に決めること

**J1. PR の分け方と、`benchmark_version` を上げる回数**

- 推奨: PR を 4 本にする(A: CI の検査、B: 0.2.2、C: 計測の部品、D: 0.3.0。P1.1)。計測に影響する変更(固定時間方式、単位、§5.5 の手順)は PR-D の 1 本にまとめ、0.3.0 への更新は 1 回にする。
- 理由: §5.5 は、待ちと判定の変更を「固定時間方式への移行(0.3.0)と同時に入れる」としている。2 本に分けると、間のビルドが「0.3.0 だが手順が違う」ものになり、同じ版同士で比べるという規則(§5.3)が崩れる。PR-D が大きくなる分は、先に PR-C で計測の部品とそのテストを入れて減らす。
- ほかの案: PR-D を「固定時間方式」と「§5.5 の手順」の 2 本に分け、どちらも 0.3.0 とする。その場合、間のビルドでは測らないと決めておく。

**J2. 0.2.2 を挟むことと、`schema_version` の番号**

- 推奨: PR-B を 0.2.2(PATCH)とし、`schema_version` を 7 にする。固定時間方式(PR-D)は 8 にする。§5.2 の「固定時間方式への移行で 7」は、PR-B で書き改める。
- 理由: 前面を離れたら無効にすることと、優先度の設定を core に寄せることは、フェーズ 0 の残りである(§8)。クロックの候補の比較とタッチの記録は、計測区間と手順を変えない(§5.3 の PATCH)。PR-D の前に入れると、(1) アプリ側の変更だけを実機で確かめられる、(2) V22 の候補のデータを早く取れる、(3) PR-D が小さくなる。JSON の形が変わるので、`schema_version` は上げる(`CLAUDE.md` の制約)。
- ほかの案: これらを PR-D に含め、7 を 1 回で済ませる(PR-D がさらに大きくなる)。

**J3. §5.2 の項目のうち、フェーズ 1 の JSON に入れる範囲**

- 背景: §5.2 は、固定時間方式の JSON に `status`、`reason`、分野別スコア(`categories`)と重み、`null`、条件の警告の一覧、`comparable` まで挙げている。一方 §8 は、分野別スコアと「値なし」をフェーズ 4 に、条件の警告と「比較可能」をフェーズ 6 に置いている。
- 推奨: §8 に従う。フェーズ 1 の JSON に入れるのは、窓ごとの値、`median_ns_per_unit`、`reference_ns_per_unit`、`window_ms`、`score_formula` と、§5.5 の記録だけにする。`category` の値だけは、フェーズ 4 に備えて `cpu_single` / `cpu_multi` に替える(§5.1)。
- 理由: CPU だけの間、分野別の総合(3:3)は、今の「4 本の幾何平均」と同じ値になる(どちらの分野もテストが 2 本のため)。先に入れても、出るスコアは変わらない。

**J4. 行列積の片: ゼロ埋めと照合を、どの片の時間に入れるか**(§5.1 が「決める」としている点)

- 推奨: 行ブロックごとに置く。行 64 本ぶんのゼロ埋めを、その行ブロックの最初の片(9 片に 1 回)に入れる。行 64 本ぶんの重みつき和を、その行ブロックの最後の片(9 片に 1 回)に入れる。9 個の和を足したものを、行列積 1 回が完成した時点で正解と照合する。窓の終わりで途中の行列積は、計測の外で最後まで実行して照合する(§4-Q5 のとおり)。
- 理由: 開発用 x86_64 環境の試作では、行ブロックごとに置くと、重い片は普通の片の +3〜9% だった。行列積ごとに置く(片 0 で全体をゼロ埋め、最後の片で全体を照合)と、片 0 が +23〜27%、最後の片が +51〜85% になった(P3.21)【実行】。単位を均質にする規則(§4-Q5 の規則 1)には、前者が合う。基準機での値は測っていない。PR-D で、片の最大時間を診断モードに出して確かめる(§1.8 (1) にある案)。
- ほかの案: 行列積ごとに置く(実装は少し単純)。ゼロ埋めと照合の時間を計測から除く(時計を止める。§5.1 の数え方から外れる)。

**J5. 「操作の直後だけ性能が上がる端末」の注意書き**(§5.5 の「記録と表示」)

- 推奨: フェーズ 1 では、開始のタッチ直後のクロックと基準クロックを記録して、要約に並べるだけにする。注意書きは出さない。出す条件(「大きく高い」の線)は、クロックの求め方が決まった後に決める(フェーズ 6)。
- 理由: 線の値が `docs/DESIGN.md` にない。現行の求め方は、状態が同じでも基準機で幅が 12〜14% ある(§1.8 (2))【実機】。HONOR 端末の 2 回目の比(2.83 ÷ 2.40 = 1.18)は、このぶれと区別しにくい。「過去の最高値の 90% 未満」の警告を、求め方が決まるまで入れないとした判断(§4-Q7)と同じ扱いにする。

**J6. 「前面を離れた」の判定**

- 推奨: Android は Activity の `onStop`(画面の回転などの構成変更によるものは除く)。iOS は `UIApplication.didEnterBackgroundNotification`。計測の実行中に起きたら、計測を中止して、結果の全体を無効にする(診断モードの計測中でも同じ)。
- 対象にしないもの: 通知の領域を引き下ろす操作のように、アプリが見えたまま一時的に操作を受けなくなる場合。アプリの外へのタッチは、アプリからは見えない。V5 (d) では、これまでどおり「触れていない」ことを依頼者に確かめる。
- 理由: §4-Q7 の 3 と V11 が想定しているのは、ホーム画面へ戻る場合である。
- 未確認: 構成変更のときの `onStop` を見分ける方法(`isChangingConfigurations`)は、記憶による【推測】。実装のセッションが、公式の資料で確かめる。

**J7. CI に足す検査**(`docs/DESIGN.md` にない追加)

- 推奨: PR-A で次の 4 つを足す。
  1. JNI の宣言とシグネチャの照合(Kotlin の宣言と `jni_bridge.cpp`)。
  2. Kotlin のパーサに、CI でビルドした CLI の JSON を読ませるテスト。
  3. Swift のパーサに、同じく CLI の JSON を読ませる検査。
  4. `analyze_summary.py` が、CLI の要約と保存済みの記録を読めることの検査。
- 理由: JNI の食い違いと、JSON とパーサの食い違いは、今の CI では見つからない。実機で動かして初めて分かる(アプリが落ちる、「JSON parse error」になる)。フェーズ 1 は JSON の形を 2 回変える。実機での往復を減らすために、先に入れる。iOS は実機がないので、3 がパーサを確かめる唯一の手段になる。
- 費用: Android と iOS の CI が、それぞれ 1〜2 分延びる見込みである【推測】。

**J8. Xcode の版の固定**(§1.5-2、§6 C6)

- 推奨: `ios.yml` のランナーを `macos-26` にし、Xcode 26.6(ビルド 17F113)を明示的に選ぶ。`xcodebuild -version` の出力が想定と違ったら、CI を失敗させる。
- 根拠: 2026-11-16 の時点で、`macos-latest` と `macos-26` は同じイメージ(macOS 26 Arm64)を指す【資料】。PR #4 の CI は、Xcode 26.6(17F113)、AppleClang 21.0.0.21000101 でビルドされた【実行】。イメージには `/Applications/Xcode_26.6.app` がある【資料】。
- 注意: GitHub がパッチ版を入れ替えると、ビルド番号が変わって CI が失敗する。そのときは、想定の値を直す PR が要る。コンパイラが黙って変わるのを防ぐための失敗である。

**J9. C8 の「build 12 と build 15 の比較」を、PR-A で行うか**

- 推奨: 行う。2 つのコミット(`8145938`、`7dcf4a1`)を CI でビルドし直し、実機で使った APK の `.so` と機械語が一致するかを確かめる。あわせて、カーネルの関数とループの位置を出して記録する。
- 理由: 実機で使った APK は Actions の成果物で、保持の期限がある。build 12 は 2026-12-11、build 15 は 2026-12-15(どちらも UTC)に消える【実行】。ビルドし直したものが一致すると確かめられれば、期限の後でも V24 の調査を続けられる。
- 影響: フェーズ 2 の調査(V24)の入口を、前に出すことになる。フェーズ 1 の実装には影響しない。見送る場合は、PR-A のプロンプトに「作業 7 のうち、一度だけの比較は行わない」と書き足して渡す(ビルドごとの位置の出力は残す)。

**J10. フェーズ 1 の実機の測り方**(V14 ほか)

- 推奨: PR-D のビルドで、端末ごとに「診断モードをオンにして 2 回 → 結果を確認 → 診断モードをオフにして 10 回」とする。V14 の変動係数は、オフの 10 回で求める。各回の間は 3 分以上あける。条件は D2 の手順 1 と同じにする(充電しない、電池 50% 以上、省電力モードはオフ、機内モード、開始のタッチの後は端末を置いて触れない)。HONOR 端末の性能モードは「標準」にする。
- 理由: V14 は「間隔を空けて 10 回」とだけ定めていて、間隔の長さと診断モードの有無を定めていない。診断モードをオフにすると、要約は 1 回 1 パートに収まる見込みで【推測】、貼り付けが 1 台あたり 10 回で済む。先に 2 回を見るのは、10 回を無駄にしないためである。
- 手順の詳細は P5。

**J11. 暫定の基準値**(`reference_times.h`)

- 推奨: 開発用 x86_64 環境で 0.3.0 の CLI を 3 回実行し、テストごとの「1 単位あたりの時間」の中央値を、有効数字 2 桁に丸めて入れる。現行と同じ「暫定値」の扱いにする。
- 理由: 基準値を実測以外で決めないという禁止事項がある(`CLAUDE.md`)。基準機の診断モードの値(§1.8 (1))は、スコア対象の手順で測ったものではないので、使わない。本測定はフェーズ 3 で行う。

**J12. フェーズ 1 での iOS の扱い**

- 推奨: iOS 側は、シミュレータ向けビルドと、J7 の 3 の検査が通るところまでにする。前面を離れたときの扱いとタッチの検出は実装するが、動作は確かめられない。「保留: iOS 実機」(§8)の確認項目に足す。
- 理由: iOS の実機検証は保留中である(`CLAUDE.md`)。

**J13. §8 に割り当てのない項目**

- §4-Q6 の 3(表示名を「持続(短時間)」などにする)と、§4-Q7 の「最適化なしのビルドは無効」は、§8 のどのフェーズにも書かれていない。
- 推奨: どちらもフェーズ 6(信頼性の表示)で入れる。フェーズ 1 では変えない。PR-D で、§8 のフェーズ 6 に書き足す。

### P0.2 実機の結果を見てから決めること

結果が出た時点で、記録の PR(PR-R)が選択肢と推奨をまとめる。ここには、今の時点の推奨の決め方を書く。

**K1. HONOR 端末が、§5.5 の手順でも「回復せず」になる場合**(D2 の未決事項)

- 見るもの: PR-D の後の 12 回のうち、ピークのテストに「回復せず」が付いた回の数。開始のタッチ直後のクロックと、基準クロック。
- 推奨の決め方: フェーズ 1 の完了は、スコアの変動係数だけで判定する(§8 の完了の条件のとおり)。「回復せず」が付いた回も含めて計算し、除いた値も並べて記録する。D2 の手順 2(警告が出た回を捨てる)の扱いは、V22 の候補の結果と合わせて、フェーズ 3 の前に決める。
- 理由: 「回復せず」は、ぶれの大きい現行のクロックの推定で決まる。スコアは、待っても待たなくても、同じ手順で測られる。

**K2. 受け入れ基準(V14)を満たさないテストがあった場合**

- §4-Q7 の 7 は、「基準値を確定する前に単位と窓を見直す」としている。単位や窓を変えると、MINOR をもう 1 つ上げることになる(0.4.0。以後のフェーズの版が 1 つずつずれる)。
- 推奨の決め方: まず原因を分ける。(1) 特定の端末だけで、端末の状態の変化による場合は、D9 により単位と窓を変えず、「不安定」として扱う(§4-Q5 の見直す条件)。(2) 両方の端末で同じテストが満たさない場合は、単位と窓を見直す。

**K3. V22 で、幅が 2% 以内の候補があった場合**

- 推奨: 0.3.0 には入れない。次に MINOR を上げるとき(フェーズ 2 の 0.4.0)に、状態の判定のクロックを入れ替える。
- 理由: 判定のクロックを替えると、採用する回が変わりうる(§5.3 の MINOR)。固定時間方式への移行と同時に替えると、V14 の結果がどちらの変更によるものか分けられない。
- 2% に届く候補がなかった場合は、次の候補を考える。確認の負荷を長くする案などは手順の変更になるので、依頼者の承認が要る。

**K4. 片の最大時間が 10 ms を超えた場合**(V21 の再確認)

- §4-Q5 の判断 2 のとおり、243 等分にする。単位の変更なので、MINOR を上げる(K2 と同じ)。

**K5. PR-B の実機の確認で、前面を離れたときの扱いやタッチの記録が、意図どおりでなかった場合**

- PR-D の前に、直す PR を挟む。

### P0.3 この計画で決めたこと

`docs/DESIGN.md` と `CLAUDE.md` からは一意に決まらず、この計画で決めた点である。詳細は P3 にある。

| 決めたこと | 節 |
|---|---|
| 窓は、ワーカーごとに「境目をまたいだ単位が終わった時刻」で区切る。単位が 1 つも終わらなかった窓のワーカーは、0 として足す | P3.2 |
| 照合はカーネルの中で行い、テストは「全部合ったか」だけを受け取る | P3.3 |
| 行列積の片の内側のループは、今の `matmul_blocked` と同じ形にする。`matmul_blocked` そのものは変えない | P3.4 |
| クロックの推定と周波数の読み取りは、6 つの窓の前と後に 1 回ずつにする。進捗の通知から「ウォームアップ」をなくし、待ちの上限を足す | P3.7 |
| 回復待ちの「合計 30 秒」に数えるのは、確かめ直しの前の 3 秒の休みだけにする。測り直しの回も、同じ 30 秒から使う | P3.8 |
| 採用しなかった回は、待ち時間、確認ごとの値、ばらつき、窓ごとの値を残す | P3.9 |
| タッチは、アプリの画面に指が触れた回数と時刻を記録する。開始のタッチは含めない | P3.11 |
| クロックの候補 (b) は、負荷の開始から 85 ms の時点で、全ワーカーが同時に推定する | P3.13 |
| 診断モードの行列サイズ掃引は残す。クラスタ別の計測に、片の最大時間を足す。`sieve`(上限 4000 万)と `matmul128` は、ビルドの間の比較(V24)のために今の形のまま残す | P3.13 |
| JSON のキーの名前と、要約の書式 | P3.14、P3.15 |
| CLI に、開始待ちを縮める指定を足す(ユニットテスト用)。CI の「全テストの実行」は既定の手順のままにする | P3.18 |
| 時間に依存するユニットテストは、作った時刻の列と、偽のテストで書く | P2.4 |

---

## P1. 全体像

### P1.1 PR の一覧

| PR | 内容 | `benchmark_version` | `schema_version` | マージ後の実機 |
|---|---|---|---|---|
| A | CI の検査を足す(Xcode の固定、3 か所の版の一致、JNI、パーサ、要約の解析、カーネルの位置) | 変えない(0.2.1) | 変えない(6) | 不要 |
| B | フェーズ 0 の残り(前面を離れたら無効、優先度の設定)、タッチの記録、クロックの候補 (a)(b) | **0.2.2** | **7** | 要(2 台。P5.1) |
| C | 固定時間方式の計測の部品と、そのユニットテスト。スコア対象の手順からは、まだ使わない | 変えない(0.2.2) | 変えない(7) | 不要 |
| D | 切り替え。固定時間方式、単位、§5.5 の手順、JSON、要約、パーサ、UI、診断モード、README | **0.3.0** | **8** | 要(2 台。P5.2) |
| R | 実機の結果の記録(B の後と D の後に、それぞれ 1 本以上) | 変えない | 変えない | — |

### P1.2 順序

`A → B → C → D` の順に、1 本ずつ `main` に入れる。

- B の実機の確認(P5.1)と、C の実装は並行して進めてよい。
- D の実装は、B と C がマージされてから始める。B の実機の確認の結果を待たずに始めてよい。確認で問題が出たら、D のマージの前に直す(K5)。
- R は、実機の結果を受け取るたびに作る。

### P1.3 フェーズ 1 に入れる項目の対応表

`docs/DESIGN.md` と `CLAUDE.md` から拾った項目と、入れる PR である。

| 出どころ | 項目 | PR |
|---|---|---|
| §5.1、§4-Q5 判断 4 | 窓を止めずに続ける計測(ウォームアップ 1 窓 + 本計測 5 窓を、1 回の連続した負荷にする) | C(部品)、D(切り替え) |
| §5.1 | 共有カウンタから取り合う方式をやめる。各ワーカーが自分の単位を繰り返す | C、D |
| §4-Q5 規則 3 | 締切を過ぎたら実行中の単位を完了し、ワーカーごとの(完了数 ÷ 実時間)を合計する | C |
| §4-Q5 判断 1・2 | 単位: 篩は上限 1,000,000、行列積は n=576 の 81 等分 | C、D |
| §5.1 | 行列積の片のゼロ埋めと照合の置き場所を決める | C(J4) |
| §5.1 | 速さの違うワーカーを模したユニットテスト | C |
| §5.1、§1.8 (3) | コアより多いワーカーが動く場合のユニットテスト | C |
| §5.1 | `Runner`: 5 窓の「1 単位あたりの時間」の中央値からスコアを出す。安定判定と測り直しは現行のまま | D |
| §5.1、D1 | 基準値の形式を「1 単位あたりの時間」に替える(値は暫定) | D(J11) |
| D9、§1.4 | 64 → 16 単位の調整を廃止する | D |
| §5.1 | 窓と窓の間で、クロックの推定、周波数の読み取り、進捗の通知をしない | C、D |
| §5.5 手順 1〜3 | 開始のタッチ直後のクロックの記録、開始待ち 10 秒、その後に基準クロック | D |
| §5.5 手順 4〜7、§4-Q7 の 4 | 2 秒の休み、確かめ直しは 2 回まで(テストごとに最大 8 秒)、合計 30 秒 | D |
| §5.5 記録 | 開始待ちの長さ、テストごとの待ち時間と確認の回数、確認ごとの値、「回復せず」、合計の上限に達したか | D |
| §5.5 記録 | 測り直しで採用しなかった回の待ち時間、確認の値、ばらつき | D |
| §5.5、§4-Q7 の 2、§8 | 計測中の画面へのタッチの記録(警告にするのはフェーズ 6) | B(記録)、D(回ごとの数) |
| §5.5 当面 | 基準クロック、回復の確認、事前負荷のクロックは、現行の求め方のまま | D(変えない) |
| §5.5 次の候補、§6 V22 | 候補 (a)(b) を、診断モードで現行の値と並べて記録する | B |
| §5.2 | JSON: `window_ms`、窓ごとの `units` と `ns_per_unit`、`median_ns_per_unit`、`reference_ns_per_unit`、`score_formula`。`runs_ns` と `median_ns` の廃止 | D |
| §5.2、`CLAUDE.md` | Kotlin と Swift のパーサを同時に直す | B、D |
| D2 | 基準値の計算に必要な値(窓ごとの単位数と時間)を、要約に整数で出す | D |
| §8 | 診断モードを新しい方式に合わせる | D |
| §1.8 (1) | 片ごとの最大時間を、診断モードに出す | D |
| §8、§1.4 | README の更新 | B(一部)、D |
| §4-Q7 の 3、§1.5-4、§8 フェーズ 0 | 計測中に前面を離れたら無効にする(V14 の前に要る) | B |
| §4-Q3、§1.5-5、§8 フェーズ 0 | 計測スレッドの優先度設定を core に寄せる | B |
| §4-Q9、§1.5-2、§6 C6 | Xcode の版の固定 | A |
| §4-Q9、§1.5-9 | 3 か所のバージョンの一致の検査 | A |
| §6 C8、§8 フェーズ 0 | カーネルの関数のアドレスと逆アセンブルを CI で出す | A |
| §5.3、§5.5 版 | `benchmark_version` を 0.3.0 にする | D |
| §8 フェーズ 1 の実機 | V4、V5 (d)、V14、V15、V22 | B の後(V22 の 1 回目)、D の後 |

### P1.4 フェーズ 1 に入れないもの

実装のセッションが、先回りして入れないようにするための一覧である。

| 項目 | 入れる時期 |
|---|---|
| 分野別スコアと重み(D1)。CPU だけの間は、今の幾何平均と同じ値になる | フェーズ 4 |
| テストの状態(`status` / `reason`)、対応の確認(`Availability`)、「値なし」(`null`) | フェーズ 4 |
| 条件の警告の一覧、`comparable`、「比較可能」「暫定」の表示 | フェーズ 6 |
| 「操作の直後だけ性能が上がる端末」の注意書き(J5) | フェーズ 6 |
| 「過去の最高値の 90% 未満」の警告(§4-Q7。求め方が決まるまで入れない) | 未定 |
| 外れた窓の数の記録、ありえない結果の警告(§4-Q7 の 6) | フェーズ 6 |
| 表示名「持続(短時間)」、最適化なしのビルドを無効にする(J13) | フェーズ 6 |
| カーネルのコード生成(ループへの指定、NEON の組み込み関数、`memset` の置き換え、独立したファイルへの分離) | フェーズ 2 |
| 行列積の 243 等分 | 必要になったとき(K4) |
| 状態の判定のクロックの入れ替え | V22 の結果の後(K3) |
| 診断モードの整理(ブーストの推移と行列サイズ掃引の削除) | フェーズ 6(§4-Q4) |
| iOS のフラグの検査、iOS の機種情報(`git_sha`、充電状態、電池残量) | P1.5 |
| 基準値の本測定 | フェーズ 3 |

### P1.5 フェーズ 0 の残りの扱い

§8 フェーズ 0 の作業のうち、まだ実装されていないものと、入れる時期である。

| 項目 | 時期 | 理由 |
|---|---|---|
| 計測中に前面を離れたら無効にする | **PR-B(フェーズ 1 の前)** | V14 の 10 回は、中断のない計測であることが前提になる |
| 3 か所のバージョンの一致の検査 | **PR-A(フェーズ 1 の前)** | PR-B と PR-D が版を上げる。上げ忘れを CI で止める |
| `ios.yml` で Xcode の版を固定する | **PR-A(フェーズ 1 の前)** | フェーズ 1 の間にランナーの既定が変わると、core の変更と区別できない失敗が出る |
| C8(カーネルの関数のアドレスと逆アセンブル) | **PR-A(フェーズ 1 の前)** | フェーズ 1 は core のコードを大きく変える。ビルドごとにカーネルの位置が動きうる(§1.8 (4))。0.2.2 と 0.3.0 のビルドから、位置の記録を残す |
| 計測スレッドの優先度設定を core に寄せる | **PR-B(フェーズ 1 の前)** | `Runner::run` の入口を PR-D が書き直す。先に済ませる |
| iOS にフラグの検査を足す | フェーズ 2 の最初 | フェーズ 1 はフラグを変えない。コード生成を扱うフェーズ 2 の前に要る |
| iOS の機種情報を補う | 「保留: iOS 実機」の再開時 | iOS の実機の結果を受け取るまで使わない |
| V23(貼り付けが切れる場所) | 手の空いたとき | 要約の分割で実害はなくなっている(§1.8 (5)) |

---

## P2. 共通の約束(全 PR)

### P2.1 読むもの

1. `CLAUDE.md`(全部)。
2. 本書の P2 と、その PR の節(P4)が挙げる P3 の節。
3. `docs/DESIGN.md` の、その PR の節が挙げる節。

### P2.2 ブランチとコミット

- 最新の `main` から作業ブランチを作る。セッションにブランチの指定があれば、それを使う。
- その PR の前提(P4 の各節)が満たされていなければ、作業を始めずに報告する。
- コミットは意味のまとまりで分ける。メッセージは英語にする(既存の履歴に合わせる)。PR の題名と説明は日本語にする。
- push と PR の作成は行う。**マージはしない。**

### P2.3 確かめ方

- core を変えたら、コミットの前に次を通す。

  ```bash
  cmake -S core -B build && cmake --build build --parallel
  ctest --test-dir build --output-on-failure
  ```

- Android と iOS は、この環境ではビルドできない。push の後、3 つのワークフロー(`core-test.yml`、`android.yml`、`ios.yml`)の結果を確かめる。失敗したら直す。3 つとも成功したことを確かめてから、完了を報告する。PR のブランチでは、同じコミットに対して `push` と `pull_request` の 2 回ずつ動く(今の設定)。どちらも見る。
- `analyze_summary.py` を変えたら、保存済みの記録に対して実行し、例外が出ないことを確かめる。

  ```bash
  python3 -I docs/measurements/tools/analyze_summary.py docs/measurements/2026-11-15-nothing-phone-1/run1-part*.txt
  ```

- JNI のシグネチャと関数名は、実行時にしか検査されない。変えたら、Kotlin の宣言と `jni_bridge.cpp` の文字列を並べて、PR の説明に書く。

### P2.4 時間に依存するユニットテストの書き方

CI のランナーは共有の環境で、時間が大きく揺れる。PR #4 では、時間に頼るテストが macOS のランナーで 4 回中 3 回失敗した(コミット `5385406` の説明)。開発用の環境でも、窓ごとの値のばらつきは 4〜32% あった(P3.21)【実行】。

- 数え方と判定の論理は、**実時間を使わずに**確かめる。
  - 窓の数え方は、作った時刻の列を `WindowRecorder` に渡して確かめる(P3.2)。
  - `Runner` の手順(中央値、ばらつき、測り直し、採用する回)は、窓ごとの値を自分で返す偽のテストで確かめる(P3.7)。
  - 回復待ちは、`recovered_ratio` を 0(必ず回復)または 1e9(決して回復しない)にして、結果を固定する(現行の `test_runner.cpp` と同じやり方)。
- 実スレッドと実時間を使うテストは、式と不変条件だけを検査する。例: ワーカーごとの単位数の合計が、カーネルが数えた数と一致する。窓の数が合う。照合が通る。
- 絶対値を検査するときは、許容差を広く取る(±20% 以上)。ばらつきや安定の判定を、実時間のテストの合否に使わない。
- 実スレッドのテストは、ワーカー 2 本を基本にする。コアより多いワーカーのテストは、不変条件だけを検査する。
- テスト用の値(短い窓、短い待ち)は、テストの中だけで設定する。`RunPolicy` の既定値と、テストの定義(`window_ms`)は変えない。

### P2.5 文書

- 事実が変わった箇所は、同じ PR で `README.md`、`docs/DESIGN.md`、`CLAUDE.md` を直す。どこを直すかは P4 の各節に書く。
- `docs/DESIGN.md` §3 の決定事項(D1〜D9)の文言は変えない。「初期値」「案」とある値を、確定と書かない。
- 実装したことは、「0.2.2 で実装した」のように、既存の書き方(§5.6、§8 フェーズ 0)に合わせて書き足す。
- 計画と違う実装にした点は、PR の説明に書き、本書の P7 に 1 行足す。
- コメントと文書は日本語、識別子と JSON のキーは英語で書く。

### P2.6 やってはいけないこと

`CLAUDE.md` の禁止事項に加えて、フェーズ 1 では次を守る。

- P1.4 の項目を入れない。
- 判定の値(95%、3%、6%、測り直し 2 回)を変えない。§5.5 の初期値(10 秒、2 秒、3 秒、2 回、30 秒)と、窓(200 ms × 5、ウォームアップ 200 ms)を変えない。
- コンパイルフラグを変えない。カーネルの内側のループの書き方を変えない(フェーズ 2 の作業)。
- 既存のカーネルの関数(`count_primes_segmented`、`matmul_blocked`、`matmul_checksum`)の本体を書き換えない。新しい関数は、別に足す。
- スコア対象のテストで、スレッドをコアに固定しない。
- 片方の OS でしか取れない情報を、待機・安定・有効性の判定に使わない。
- 計測区間(6 つの窓の最初から最後まで)の中で、メモリの確保、ログの出力、ファイル I/O、JNI / Objective-C の呼び出しをしない。
- 基準値を、実測以外で決めない(J11)。

### P2.7 迷ったとき

- 外から見える点(JSON のキー、要約の書式、手順の値、判定、UI の文言の意味)で、本書と `docs/DESIGN.md` から決まらないものは、進めずに依頼者に尋ねる。
- 内部の構造、識別子の名前、ファイルの分け方は、任せる。本書の名前と違うものにしたら、PR の説明に書く。

### P2.8 PR の説明に書くこと

1. 概要。
2. 変更内容。
3. 確認(手元の `ctest` の結果、3 つのワークフローの結果)。
4. 版(`benchmark_version`、`schema_version` をどうしたか)。
5. 計画との差(なければ「なし」)。
6. マージ後に実機で確かめること(本書の P5 の該当する部分。実機の確認が要らない PR は、そう書く)。

---

## P3. 設計の詳細(この計画で決めたこと)

識別子の名前は、この計画での呼び名である。実装で変えてもよい(P2.7)。JSON のキーと要約の書式は、外から見えるので、変えるときは PR の説明に書く。

### P3.1 定数

| 名前 | 値 | 置き場所 | 備考 |
|---|---|---|---|
| `kWarmupWindows` | 1 | `runner.h` | `kWarmupRuns` を置き換える |
| `kMeasuredWindows` | 5 | `runner.h` | `kMeasuredRuns` を置き換える |
| CPU テストの `window_ms` | 200 | `registry.cpp` | `TestInfo.window_ms` に入れる。テストの定義の一部(§4-Q5 規則 6) |
| 篩の単位 | 上限 1,000,000(素数は 78,498 個)、区間 32 KiB | `cpu_kernels.h` | 区間の大きさは変えない(§4-Q5) |
| 行列積の単位 | n=576、ブロック 64、81 片 | `cpu_kernels.h` | n は変えない(§4-Q5) |
| `RunPolicy::start_wait_ms` | 10000 | `runner.h` | 新規。§5.5 手順 2 |
| `RunPolicy::min_rest_ms` | 2000 | 同上 | 変えない |
| `RunPolicy::check_interval_ms` | 3000 | 同上 | 変えない |
| `RunPolicy::max_rechecks` | 2 | 同上 | 新規。`max_wait_ms`(60000)を置き換える |
| `RunPolicy::recheck_budget_ms` | 30000 | 同上 | 新規。§5.5 手順 7 |
| `recovered_ratio`、事前負荷の値、安定判定の値 | 現行のまま | 同上 | 変えない |

基準機での見込み(§1.8 (1) の単位時間からの換算。スレッドがコアに 1 本ずつ載ると仮定)【推測】:

| テスト | 窓 1 つの単位数 | 1 単位あたりの時間 |
|---|---|---|
| `cpu.single.sieve` | 約 150 | 約 1.33 ms |
| `cpu.single.matmul` | 約 300 | 約 0.67 ms |
| `cpu.multi.sieve` | 約 750 | 約 0.26 ms |
| `cpu.multi.matmul` | 約 1,250 | 約 0.16 ms |

### P3.2 窓の数え方

§5.1 の「窓は止めずに続ける」と、§4-Q5 の規則 3 を、次の形にする。`docs/measurements/tools/simulate_units.py` の `fixed_time_continuous` と同じ数え方である。

```cpp
// core/src/window_recorder.h (新規)。ワーカー 1 本ぶんの数え方。
// 時刻は呼び出し側が渡す (ユニットテストでは、作った時刻を渡す)
struct WorkerWindow {
  std::int64_t units = 0;        // この窓で完了した単位数
  std::int64_t ns = 0;           // この窓の実時間 (前の区切りから、この区切りまで)
  std::uint64_t cpu_mask = 0;    // 単位の開始時に見た CPU 番号 (bit i = cpu i。64 以上と不明は入れない)
  std::int64_t unit_ns_min = 0;  // この窓でいちばん短かった 1 単位
  std::int64_t unit_ns_max = 0;  // この窓でいちばん長かった 1 単位
};

class WindowRecorder {
 public:
  // bounds: 窓の境目の時刻 (昇順、count 個)。out: count 個の出力先 (呼び出し側が事前に確保する)。
  // start_ns: このワーカーが最初の単位を始めた時刻
  void reset(const std::int64_t* bounds, int count, WorkerWindow* out, std::int64_t start_ns);
  // 単位が 1 つ終わるたびに呼ぶ。now_ns は終わった時刻、cpu はその単位の開始時の CPU 番号。
  // 最後の境目を越えたら false を返す (ワーカーは止まる)
  bool on_unit_done(std::int64_t now_ns, int cpu);
};
```

規則:

1. 境目は全ワーカーで共通にする。`bounds[k] = t0 + (k + 1) × window_ns`(k = 0 … W − 1。W はウォームアップの窓と本計測の窓の合計で、既定は 6)。`t0` は、ワーカーを起こす直前に呼び出し側が読んだ時刻である。
2. 単位が終わった時刻 `now` が `bounds[k]` 以上なら、窓 k を閉じる。その単位は窓 k に数える。窓 k の実時間は、前の区切りの時刻から `now` までである。最初の窓の「前の区切り」は `start_ns` である。
3. 1 つの単位が複数の境目をまたいだら、2 つ目以降の窓は `units = 0`、`ns = 0` で閉じる。
4. ワーカーは、窓を閉じても止まらずに次の単位へ進む。最後の窓を閉じたら止まる。
5. 窓 k のスループットは、全ワーカーの `units ÷ ns × 10^9` の合計とする。`ns` が 0 のワーカーは、0 として足す。
6. 窓 k の「1 単位あたりの時間」は `round(10^9 ÷ スループット)` とし、整数の ns で持つ。スループットが 0 なら 0 とする。本計測の窓に 0 が 1 つでもあれば、その回は無効にする。
7. 時刻の読み取りは、1 単位につき 1 回にする(単位の終わり = 次の単位の始まり)。CPU 番号の読み取り(`platform::current_cpu()`)も、1 単位につき 1 回にする(現行と同じ)。
8. 窓ごとの記録(`WorkerWindow`)は、計測の前に確保する。計測区間の中では確保しない。

5 と 6 を計算する関数は、1 つにまとめる(`WindowRecorder` のテストと、`TimedUnitTest` が同じものを使う)。

ウォームアップの窓(k = 0)の値は、記録するだけで、スコアには使わない。

### P3.3 カーネルのインターフェース

```cpp
// core/src/tests/timed_kernels.h (新規)
class TimedKernel {
 public:
  virtual ~TimedKernel() = default;
  virtual void prepare(int workers) = 0;   // 計測の外: 作業領域の確保、入力の生成
  virtual void begin(int worker) = 0;      // 計測の外: 連続実行の前に、worker の状態を初期化する
  virtual void run_unit(int worker) = 0;   // 計測区間: 1 単位。照合できるまとまりが完成したら、その場で照合する
  virtual void finish(int worker) = 0;     // 計測の外: 途中のまとまりを最後まで実行して、照合する
  virtual bool checksum_ok(int worker) const = 0;       // begin 以降に 1 回以上照合し、すべて一致したか
  virtual std::uint64_t expected_checksum() const = 0;  // 照合 1 回ぶんの正しい値 (記録用)
  // 最後に照合した値。食い違いがあったら、最初に食い違った値
  virtual std::uint64_t observed_checksum(int worker) const = 0;
  virtual void release() {}
};
```

- ワーカーごとの状態は、`alignas(64)` で分ける(現行の `Partial` と同じ理由)。
- `run_unit` の中で、メモリを確保しない。
- 現行の `UnitKernel` は、PR-C では残す(スコア対象の手順が使っている)。PR-D で、`TimedKernel` に一本化して消す。

### P3.4 行列積の片(`MatmulPieceKernel`)

**単位**: n=576 の行列積 1 回を、外側 2 重のブロックの 1 組(行 64 本ぶん × 中間の添字 64 個ぶん)に分けた 1 片(§4-Q5)。片の番号 p(0〜80)から、行ブロック `bi = p / 9` と中間のブロック `bk = p % 9` が決まる。n は 64 の倍数なら何でもよい形にする(片の数は `(n / 64)^2`。ユニットテストと、診断モードの行列サイズ掃引で使う)。

**1 片の処理**(J4):

```cpp
// ワーカーごとの状態: piece (0 〜 片の数 - 1)、acc (行ブロックの和の累計)、verified (照合した回数)、ok、first_bad
void MatmulPieceKernel::run_unit(int worker) {
  // ii = bi * 64, kk = bk * 64
  if (bk == 0) /* この行ブロックの最初の片 */ 行 [ii, ii + 64) をゼロにする (std::memset。64 * n * 8 バイト);
  matmul_piece(a, b, c, n, 64, ii, kk);
  if (bk == 最後) /* この行ブロックの最後の片 */ acc += matmul_checksum_rows(c, n, ii, ii + 64);
  if (++piece == 片の数) {   // 行列積 1 回が完成した
    acc を expected と比べる (食い違ったら ok = false にし、最初の値を first_bad に残す);
    ++verified; acc = 0; piece = 0;
  }
}
```

- `matmul_piece` の内側の 4 重ループ(jj、i、k、j)は、`matmul_blocked`(`cpu_matmul.cpp:31-41`)と同じ形で書く。入口で `launder(n)`、出口で `clobber_memory()` を呼ぶのも同じにする。

  ```cpp
  void matmul_piece(const double* a, const double* b, double* c, int n, int block, int ii, int kk) {
    launder(n);
    const int i_end = std::min(ii + block, n);
    const int k_end = std::min(kk + block, n);
    for (int jj = 0; jj < n; jj += block) {
      const int j_end = std::min(jj + block, n);
      for (int i = ii; i < i_end; ++i) {
        double* c_row = c + static_cast<size_t>(i) * n;
        const double* a_row = a + static_cast<size_t>(i) * n;
        for (int k = kk; k < k_end; ++k) {
          const double aik = a_row[k];
          const double* b_row = b + static_cast<size_t>(k) * n;
          for (int j = jj; j < j_end; ++j) c_row[j] += aik * b_row[j];
        }
      }
    }
    clobber_memory();
  }
  ```

- `matmul_checksum_rows(c, n, i0, i1)` は、`matmul_checksum`(`cpu_matmul.cpp:48-58`)の行の範囲を限ったものである。行ごとの和も、行ブロックの和も整数なので、9 個を整数で足した値は、全体の `matmul_checksum` と一致する。正解は、今の `matmul_expected_checksum` をそのまま使う。
- ゼロ埋めは、今と同じ `std::memset` を使う。置き換えはフェーズ 2 の作業である(§1.5-11)。
- **`matmul_blocked` と `matmul_checksum` の本体は変えない。** 診断モードの `matmul128` は、0.2.x の記録と比べるために、今の `MatmulKernel::run_unit` と同じ処理(全体のゼロ埋め → `matmul_blocked` → `matmul_checksum`)を使い続ける(P3.13)。

**`finish(worker)`**: `piece != 0` の間、`run_unit(worker)` を繰り返す。途中の行列積が完成し、照合される。計測の外で行う。

- 途中の行列積は、最大で 80 片残る。基準機の遅いコアでは、最大で約 0.57 秒かかる見込みである(80 × 7.05 ms。換算)【推測】。各ワーカーが自分の分を並行して片づける。計測の後なので、スコアには入らない。

**`begin(worker)`**: `piece = 0`、`acc = 0`、`verified = 0`、`ok = true` に戻す。

**規則 4・5 との関係**: 単位の定義が変わる(ゼロ埋めと照合の置き場所が変わる)ので、V21 の値(遅いコアで 7.02〜7.09 ms)は、PR-D のビルドで測り直す(P3.13、P5.2)。

### P3.5 篩の単位(`SieveTimedKernel`)

- `SieveTimedKernel(limit, expected)`。`run_unit` は `count_primes_segmented(limit, kSieveSegmentBytes, scratch)` を 1 回呼び、戻り値を `expected` と比べる。1 単位が、そのまま照合 1 回になる。`finish` は何もしない。
- スコア対象は `limit = 1,000,000`、`expected = 78,498`。診断モードの `sieve`(上限 4000 万)も、同じクラスで作る。
- `count_primes_segmented` の本体は変えない。作業領域の確保(`reserve`)は、今の `SieveKernel::prepare` と同じにする。

### P3.6 計測の部品(`TimedUnitTest`)

`UnitBasedTest` の置き換えである。PR-C では独立したクラスとして足し、PR-D で `BenchmarkTest` を実装する形にして、既定スイートから使う。

```cpp
struct WindowOutput {
  std::int64_t units = 0;        // 完了した単位数 (全ワーカーの合計)
  double units_per_sec = 0.0;    // ワーカーごとの (完了数 ÷ 実時間) の合計
  std::int64_t ns_per_unit = 0;  // round(1e9 / units_per_sec)。0 = 単位が 1 つも終わらなかった
};
struct WindowedRun {
  std::vector<WindowOutput> windows;  // ウォームアップの窓 + 本計測の窓
  bool checksum_ok = false;           // 全ワーカーの照合が通ったか (計測の外で完了させた分を含む)
};

WindowedRun TimedUnitTest::run_windows(std::int64_t window_ns, int warmup_windows, int measured_windows);
```

`run_windows` の流れ:

1. (計測の外)ワーカーごとの記録の領域(`WorkerWindow` を W 個)と、境目の配列を用意する。
2. `t0` を読み、境目を決める。
3. 全ワーカーを起こす(マルチは `WorkerPool`、シングルは呼び出しスレッドだけ)。各ワーカーは次を行う。
   1. `kernel->begin(w)`。
   2. 自分の開始時刻を読み、`WindowRecorder::reset`。
   3. 「CPU 番号を読む → `kernel->run_unit(w)` → 時刻を読む → `on_unit_done`」を、`false` が返るまで繰り返す。
   4. (計測の外)`kernel->finish(w)`。
4. 全ワーカーの終了を待ち、窓ごとに P3.2 の 5 と 6 を計算する。`checksum_ok` は、全ワーカーの `kernel->checksum_ok(w)` の論理積である。

- 共有カウンタは使わない。ワーカーの間で共有して書き込むものは、ない。
- ワーカー数は現行と同じにする(マルチは `ctx.threads`、シングルは 1)。コアには固定しない。
- 診断用の、負荷中のクロックの記録(`enable_inload_probe`。5 ms ごとに `quick_clock_ghz`)は、同じ指定で残す。スコア対象のテストでは使わない。
- 最後の境目を越えたワーカーは、先に `finish` に進む。ほかのワーカーの最後の単位が終わるのを待たない。単位が規則 4 を満たす大きさなら、影響は小さい(§1.6(b) のシミュレーションで、止めずに続ける方式の偏りは 0.2% 以内)。

### P3.7 `BenchmarkTest` と `Runner`(PR-D)

**`BenchmarkTest`**(`benchmark_test.h`):

```cpp
struct TestInfo {
  std::string id, name;
  std::string category;                    // "cpu_single" / "cpu_multi" (J3)
  std::int64_t reference_ns_per_unit = 0;  // 基準機 (ピーク段階) の 1 単位あたりの時間 (reference_times.h)
  int window_ms = 0;                       // 窓の長さ
};

class BenchmarkTest {
  // info / threads_used / setup / before_run / after_run / take_telemetry / teardown は現行どおり
  virtual WindowedRun run_windows(std::int64_t window_ns, int warmup_windows, int measured_windows) = 0;  // run_once を置き換える
  virtual std::uint64_t expected_checksum() const = 0;       // 照合 1 回ぶんの正しい値
  virtual std::uint64_t observed_checksum() const { return 0; }
};
```

`Availability`(§5.1 の案にある)は入れない(P1.4)。

**1 回(1 つの試行)の流れ**:

```
test.setup(ctx)
conditions_before を取る
進捗を通知する (この回で 1 回だけ)
test.before_run()            // クロックの推定 (計測の外)
周波数の上限と現在値を読む    // Android / Linux だけ。記録用
run = test.run_windows(window_ms * 1e6, kWarmupWindows, kMeasuredWindows)   // 約 1.2 秒。途中で何も挟まない
現在の周波数を読む
test.after_run()             // クロックの推定 (計測の外)
telemetry = test.take_telemetry()
conditions_after を取る
test.teardown()
```

- 本計測の窓の `ns_per_unit` の中央値を `median_ns_per_unit` とする。スコアは `reference_ns_per_unit ÷ median_ns_per_unit × 1000`。
- ばらつきは、本計測の窓の `ns_per_unit` の(最大 − 最小)÷ 中央値とする。6% 以下なら安定とする(現行の判定を、窓にそのまま移す)。
- 有効の条件は、`checksum_ok` が真、最後まで完了、本計測の窓に `ns_per_unit = 0` がない、の 3 つとする。返ってきた窓の数が合わないときも、無効にする(`error` に理由を入れる)。
- 段階のスコア(テストのスコアの幾何平均)、持続率、`overall_score` の計算は変えない。
- テストの `ghz` は、前後のクロックの推定(全ワーカー)のうち、正の値の平均とする。マルチでは、状態の指標に使わない(§4-Q3)。
- 取り消しと中断(P3.10)は、回と回の間、休みと確認の間で見る。6 つの窓の途中では見ない(最長で、約 1.2 秒と完了処理の時間だけ遅れる)。
- 進捗の通知は、単調に増え、最後に `total_steps` に達すればよい。配分は任せる。

**`Progress`**:

```cpp
struct Progress {
  std::string phase;       // "prepare" / "start_wait" / "peak" / "sustained" / "diagnostics"
  int test_index = 0, test_count = 0;
  std::string test_id, test_name;
  int step = 0, total_steps = 0;
  int wait_ms = -1;        // 開始待ち / 回復待ち / 事前負荷の経過時間 (それ以外は -1)
  int wait_limit_ms = -1;  // その待ちの上限 (開始待ち: start_wait_ms。回復待ち: min_rest_ms + max_rechecks × check_interval_ms。事前負荷: precondition_max_ms)
  int attempt = 1;
};
```

`warmup` はなくす(窓ごとの通知をしないため)。JNI のシグネチャと、Objective-C のブロックの型が変わる(P2.3)。

**ユニットテスト用の入口**: `run_tests` と `run_with_factory` は残す。偽のテストは `run_windows` を実装し、窓ごとの値を自分で返す(P2.4)。

### P3.8 ピーク段階の手順(PR-D)

§5.5 の手順を、次の形にする。

```
(ピーク段階があり、正式な手順のとき)
1. phase "prepare":    start_touch_ghz = loaded_probe(probe_load_ms)     // 記録するだけ
2. phase "start_wait": start_wait_ms だけ待つ。負荷をかけない。1 秒ごとに進捗を通知する (wait_ms、wait_limit_ms)
3. phase "prepare":    baseline_ghz = loaded_probe(probe_load_ms)

(ピークの各テストの、各回の前)
4. min_rest_ms だけ休む。waited = min_rest_ms
5. checks = []; rechecks = 0
   loop:
     g = loaded_probe(probe_load_ms); checks に足す
     if baseline_ghz <= 0 または g >= baseline_ghz × recovered_ratio: recovered = true; 抜ける
     if rechecks >= max_rechecks: 抜ける                                  // テストごとの上限 (手順 6)
     if recheck_used_ms + check_interval_ms > recheck_budget_ms:          // 合計の上限 (手順 7)
        budget_exhausted = true; 抜ける
     check_interval_ms だけ休む
     waited += check_interval_ms; recheck_used_ms += check_interval_ms; ++rechecks
6. recovered でなければ「回復せず」と記録して、そのまま測る
```

- `loaded_probe` は、現行の求め方(`runner.cpp:75-82`)のまま使う。基準クロック、回復の確認、事前負荷の 3 つとも同じである。
- 合計の上限(30 秒)に数えるのは、確かめ直しの前の休み(`check_interval_ms`)だけにする。2 秒の休みと、確認の負荷(100 ms)は数えない。§5.5 の「2 秒 × 9 + 30 秒 = 48 秒」という計算と合う。
- 測り直しの回も、同じ手順 4〜6 を行い、同じ 30 秒から使う。
- 持続段階だけを実行するとき(ユニットテスト)は、手順 1〜3 を行わない。
- 取り消しと中断は、休みの間も見る(現行の `sleep_ms` と同じ 50 ms 刻み)。
- 持続段階の手順(事前負荷 3〜10 秒、3%)は変えない。
- 全体の記録: 開始のタッチ直後のクロック、開始待ちの長さ、基準クロック、確かめ直しに使った時間の合計、合計の上限に達したか(P3.14 の `config`)。回ごとの記録は P3.9 にある。

### P3.9 安定判定と、採用する回の記録(PR-D)

- 判定(6%)、測り直し(2 回まで)、採用の規則(ばらつきが最小の回。`runner.cpp:357-361`)は変えない。
- すべての回について、次を残す(採用した回を含む)。

  ```cpp
  struct AttemptRecord {
    int attempt = 1;                 // 1 始まり
    bool adopted = false;
    int wait_ms = -1;                // 回復待ちの時間 (待っていなければ -1)
    std::vector<double> check_ghz;   // 確認ごとの値 (順に)
    bool recovered = true;
    bool recheck_budget_exhausted = false;
    bool valid = false, stable = false;
    double spread = 0.0;
    std::int64_t median_ns_per_unit = 0;
    std::vector<std::int64_t> ns_per_unit;  // 本計測の窓
    std::vector<std::int64_t> units;        // 本計測の窓
    int touches = 0;                 // この回の間 (休みの開始から、計測の終わりまで) の画面へのタッチの数
    std::int64_t t_start_ms = 0;     // 負荷の開始 (スイートの開始から)
  };
  ```

- 採用した回の細かい内訳(ワーカーごとの値、周波数、端末の状態)は、今と同じくテストの結果そのものに入れる。

### P3.10 前面を離れたときの扱い(PR-B)

- core に `void Runner::interrupt(const std::string& reason)` を足す。別のスレッドから呼んでよい。`cancel()` と同じ箇所で見て、計測を中止する。
- 結果: `interrupted = true`、`interruption = reason`(`"left_foreground"`)、`valid = false`。`cancelled` は、利用者が中止したときだけ真にする。
- それまでに終わったテストの結果は、中止のときと同じく残す。
- 診断モードの計測中に起きても、同じ扱いにする(J6)。
- Android: `MainActivity.onStop()` で、構成変更でなければ(`!isChangingConfigurations`)、ViewModel 経由で `interrupt("left_foreground")` を呼ぶ。計測中でなければ何もしない。
- iOS: `UIApplication.didEnterBackgroundNotification` を ViewModel が受けて、同じく呼ぶ。
- 画面には、中止(利用者)、前面を離れた、チェックサム不一致を、分けて出す。

### P3.11 画面へのタッチの記録(PR-B)

- core に `void Runner::note_touch()` を足す。別のスレッドから呼んでよい。`run()` の実行中だけ、スイートの開始からの時刻(ms)を記録する。実行中でなければ無視する。
- 記録: 全体の回数(`touches`)と、時刻の列(`touch_ms`。先頭の 100 個まで)。テストごとの回数(`tests[].touches`。そのテストの最初の休みの開始から、最後の計測の終わりまで)。PR-D で、回ごとの回数(P3.9)に細かくする。
- 判定には使わない。スコアにも使わない。警告にするのはフェーズ 6 である。
- ミューテックスで守ってよい。計測スレッドがこの記録を読むのは、計測区間の外(回の前後)だけにする。
- Android: `MainActivity.dispatchTouchEvent` で、`ACTION_DOWN` のときに ViewModel 経由で呼ぶ。その後、`super.dispatchTouchEvent` に渡す(操作は妨げない)。
- iOS: ウィンドウに、触れ始めを見るだけのジェスチャ認識(`UIGestureRecognizer` のサブクラス。`touchesBegan` で通知して、すぐ `.failed` にする。`cancelsTouchesInView = false`)を付ける。ほかの方法でもよいが、スクロールやボタンの操作を妨げないこと。動作は実機で確かめられない(J12)。
- 開始のタッチは入らない(指が触れた時点では、まだ計測が始まっていない)。「中止」ボタンへのタッチは入る。
- アプリの外へのタッチ(通知の領域など)は記録できない(J6)。

### P3.12 計測スレッドの設定(PR-B)

- `Runner::run`(`run_impl` の入口)で、呼び出しスレッドに `platform::configure_benchmark_thread()` を呼ぶ(§4-Q3)。iOS では QoS を `USER_INITIATED` にする。Android と Linux では何もしない(現行どおり)。
- Swift 側の `thread.qualityOfService = .userInitiated`(`BenchmarkViewModel.swift:71`)は消す。設定の場所を core の 1 か所にする。
- 計測には影響しない(iOS は今も同じ QoS で動いている。Android は変わらない)。

### P3.13 診断モード

**(1) 状態を確かめるクロックの候補(PR-B)**

`run_clock_probe_comparison`(`diagnostics.cpp:137-187`)を、次のように変える。不採用になった「加算の数を数える」求め方(`counted`、`busy_spin_counted`)は消す。

- 回数(12)、休み(2 秒)、負荷の長さ(100 ms)は変えない。
- 1 回の負荷で、各ワーカーは次を行う。`T0` は、ワーカーを起こす直前に読んだ共通の時刻である。
  1. `T0 + inload_at_ms` まで負荷をかける(`inload_at_ms = max(load_ms − 15, load_ms / 2)`。既定では 85 ms)。
  2. その場で `estimate_clock_ghz()` を呼ぶ。これが **候補 (b)** の値である。全ワーカーが同じ時刻に推定し、推定の間も全ワーカーが動いている。
  3. 自分の開始から `load_ms` が過ぎるまで、負荷を続ける。
  4. `estimate_clock_ghz()` を呼ぶ。これが **現行** の値である(今と同じ)。
- 記録するのは、現行の値と候補 (b) の値の、それぞれの平均、最小、最大である。**候補 (a)**(ワーカーごとの推定値の最大)は、現行の値の最大として出る。
- 要約の集計の行に、現行の平均、候補 (a)、候補 (b) の平均、候補 (b) の最大の 4 つについて、12 個の平均と幅を出す(P3.15)。
- 判定には使わない。

**(2) クラスタ別の計測(PR-D)**

| 名前 | 単位 | 1 スレッドあたりの単位数 | 目的 |
|---|---|---|---|
| `sieve` | 篩 上限 4000 万(今と同じ) | 6 | ビルドの間の比較(V24)。0.2.x の記録と比べる |
| `matmul128` | 行列積 n=128 の 1 回(今の `MatmulKernel` と同じ処理) | 300 | 同上 |
| `sieve1m` | 篩 上限 100 万(スコア対象の単位) | 60 | 規則 4・5 の確認 |
| `matmul576p` | 行列積 n=576 の 1 片(スコア対象の単位) | 243(行列積 3 回ぶん。その前に 81 片のウォームアップ) | 規則 4・5 の確認(V21 の測り直し) |

- `matmul576`(1 回を単位にしたもの)は、`matmul576p` に置き換える。
- 計測の前のウォームアップは、`matmul576p` が 81 片、ほかは今と同じ 1 単位とする。
- すべての計測に、いちばん長かった 1 単位の時間(`max_ms`)を足す。`matmul576p` の `max_ms` が、片の最大時間である(§1.8 (1) の案)。
- `sieve` と `matmul128` の処理は、今と同じにする。遅いコアの `sieve` は、ビルドによって約 3 割変わった(§1.8 (4))。同じ計測を続けることで、PR ごとのビルドで変化を追える。

**(3) 行列サイズ掃引(PR-D)**

- 残す(削除はフェーズ 6。§4-Q4)。`MatmulPieceKernel(n)` と `run_windows` に載せ替える。GFLOPS は `2 × 64 × 64 × n × 単位/秒 ÷ 10^9` で求める。
- 窓は 200 ms、ウォームアップ 1、本計測 3 とする(診断だけの値)。
- 負荷中のクロック(`enable_inload_probe`)は、今と同じく記録する。
- JSON の `st_units`、`mt_units`、`st_runs_ns`、`mt_runs_ns`、`st_median_ns`、`mt_median_ns` は消す。パーサが読んでいるキー(`n`、`threads`、`st_working_set_bytes`、`mt_working_set_bytes`、`st_gflops`、`mt_gflops`、`valid`)は残す。

**(4) ブーストの推移、周波数の定期記録**: 変えない(ブーストの推移は、篩 上限 4000 万の単位のまま)。

### P3.14 JSON

**schema 7(PR-B)**: schema 6 に、次を足す。

| 場所 | キー | 内容 |
|---|---|---|
| 最上位 | `interrupted` | 真偽。前面を離れて中止したか |
| 最上位 | `interruption` | 理由(`"left_foreground"`)。なければ空文字列 |
| 最上位 | `input` | `{"touches": 回数, "touch_ms": [スイートの開始からの ms、先頭の 100 個まで]}` |
| `tests[]` | `touches` | そのテストの間のタッチの数 |
| `diagnostics.clock_probe` | `inload_at_ms` | 候補 (b) を推定する時点 |
| `diagnostics.clock_probe.samples[]` | `inload_ghz`、`inload_min`、`inload_max` | 候補 (b) の値 |

消すもの: `diagnostics.clock_probe.count_from_ms`、`samples[].counted_ghz`、`counted_min`、`counted_max`。

**schema 8(PR-D)**: 次のように変える。

| 場所 | 変更 |
|---|---|
| 最上位 | `score_formula` を `"reference_ns_per_unit / median_ns_per_unit * 1000"` にする |
| `config` | `warmup_runs`、`measured_runs` を消し、`warmup_windows`、`measured_windows` を足す |
| `config` | `start_touch_ghz`、`start_wait_ms` を足す。`baseline_ghz` は残す |
| `config` | `recheck` を足す: `{"max_per_attempt", "interval_ms", "budget_ms", "used_ms", "exhausted"}` |
| `tests[]` | `reference_ns`、`warmup_ns`、`runs_ns`、`median_ns`、`run_details` を消す |
| `tests[]` | `window_ms`、`reference_ns_per_unit`、`median_ns_per_unit` を足す |
| `tests[]` | `category` の値を `"cpu_single"` / `"cpu_multi"` にする |
| `tests[]` | `check_ghz`(採用した回の確認ごとの値)、`recheck_budget_exhausted` を足す。`wait_ms`、`recovered`、`recovery_ghz`(最後の確認の値)は残す |
| `tests[]` | `checksum_ok` を足す。`checksum`(観測した値)と `expected_checksum`(照合 1 回ぶんの正しい値)は、文字列のまま残す |
| `tests[]` | `t_start_ms`、`cpu_khz_before`、`cpu_khz_after`、`cpu_cap_khz` を、テストの直下に置く(回ごとではなくなるため) |
| `tests[]` | `workers`: `[{"worker", "ghz_before", "ghz_after"}]`(診断のときは `ghz_load_avg`、`ghz_load_max` も) |
| `tests[]` | `windows`: `[{"warmup", "units", "ns_per_unit", "units_per_sec", "workers": [{"units", "ns", "cpus", "unit_ns_min", "unit_ns_max"}]}]` |
| `tests[]` | `attempt_log`: P3.9 の `AttemptRecord` の配列(キーは構造体のメンバーと同じ名前) |
| `diagnostics.clusters[].measures[]` | `max_ms_per_unit` を足す。`kernel` に `"matmul576p"` が加わり、`"matmul576"` がなくなる |
| `diagnostics.matmul_sweep[]` | P3.13 (3) のとおり |

- 小数は `JsonWriter::format_fixed` で出す。`units_per_sec` は小数 3 桁、クロックは小数 3 桁にする。
- `status`、`reason`、`categories`、`null`、`comparable` は入れない(J3)。無効なスコアは、今と同じく 0 で出す。

### P3.15 要約テキスト

1 パート 8,000 バイト以内、まとまりを途中で切らない、という §5.6 の規則は変えない。`test_summary.cpp` の大きさの検査(テスト 9 本 × 2 段階、診断あり、論理コア 16 個)は、新しい書式でも通す。

**PR-B(0.2.2)で足すもの**(ほかは 0.2.1 と同じ):

```
scores peak 799.5 sustained 799.3 retention 100.0% valid=no INTERRUPTED(left_foreground) cores=8 diag=yes
input touches 3 at_ms 5120 5630 20411
[peak cpu.single.sieve] median 250.0ms score 600.0 ... rec 2.35G touches 2
  probe rest 2000ms load 100ms workers 8 | current = mean of per-worker estimate after load, inload = mean of per-worker estimate at 85ms while all workers keep loading (GHz, per-worker min-max)
    2020ms current 2.308 (1.80-2.52) inload 2.116 (1.80-2.52)
  probe 12 samples: current mean 2.293 range 12.1% | (a) current-max mean 2.515 range 0.4% | (b) inload mean 2.116 range 1.5% | inload-max mean 2.515 range 0.3% (range = (max-min)/mean)
```

- `INTERRUPTED(理由)` は、中断したときだけ出す(`CANCELLED` と同じ位置)。
- `input` の行は、`baseline` の行の次に、いつも出す。`at_ms` は先頭の 10 個までにし、0 回なら `input touches 0` とする。
- テストの見出しの `touches N` は、1 回以上のときだけ出す。
- 例の数値は、書式を示すためのもので、実測ではない。

**PR-D(0.3.0)の書式**:

```
MobileBench v0.3.0 | app 0.3.0 build 30 (abc1234) | run 2026-11-20T01:23:45Z | part 1/2
device: ... (今と同じ)
os ... (今と同じ)
scores peak 799.5 sustained 799.3 retention 100.0% valid=yes cores=8 diag=yes
clock touch 2.31GHz -> wait 10000ms -> baseline 2.28GHz(all-core) | recheck used 0/30000ms | precondition 3000ms -> 2.25GHz settled
input touches 0
topology max_mhz 1804x4 2400x3 2515 capacity 378x4 957x3 1024
columns: window units ns/unit cpu | MT: w = units per worker (min-max) | umax = longest unit (ms)
cond(base) ...
cond: only keys that differ from cond(base) ('=' same as base)
[peak cpu.single.sieve] npu 1333211 ref 1300000 score 975.1 threads 1 win 200ms @2.51G spread 0.1% attempts 1 wait 2000ms chk 2.35
  cond = -> thermal_headroom_10s=0.40
  ghz 2.40>2.51 cur 691x7 2515
  W  148u 1351022ns cpu4,7 umax 2.10
  #1 150u 1333211ns cpu7 umax 1.36
  #2 150u 1333305ns cpu7 umax 1.35
  #3 150u 1333190ns cpu7 umax 1.36
  #4 150u 1333402ns cpu7 umax 1.35
  #5 150u 1333118ns cpu7 umax 1.36
[peak cpu.multi.sieve] npu 264900 ref 230000 score 868.3 threads 8 win 200ms @2.21G spread 6.2% UNSTABLE attempts 3 wait 8000ms(NOT recovered) chk 2.10 2.12 2.11
  cond thermal_headroom_10s=0.39 -> thermal_headroom_10s=0.38
  ghz 2.31>2.26(1.79-2.51) cur 1516x4 2400x3 1862
  W  741u 269900ns cpu0-7 w 47-152 umax 6.20
  #1 755u 264900ns cpu0-7 w 48-150 umax 4.31
  ...
  rej 1 wait 8000ms(NOT recovered) chk 2.11 2.10 2.12 spread 7.9% npu 241000 240800 259900 261900 262100
  rej 3 wait 5000ms(NOT recovered)(budget) chk 2.10 2.11 spread 6.4% npu 262000 250100 263000 266100 262100 touches 1
```

- `npu` は `median_ns_per_unit`、`ref` は `reference_ns_per_unit` で、どちらも整数の ns である。窓の行の `u` の前は単位数、`ns` の前は `ns_per_unit` である。基準値の計算に要る値を、丸めずに出す(D2)。
- `chk` は確認ごとの値(GHz。小数 2 桁)を順に並べる。回復待ちのないテスト(持続)では、`wait` と `chk` を出さない(`rej` の行も同じ)。
- 待ちの後ろの印: `(NOT recovered)` は回復しなかった、`(budget)` は合計の上限で確かめ直しを打ち切った、である。両方に当たれば両方出す。
- `ghz` の行は、6 つの窓の前後のクロックの推定(今の各回の行と同じ形)、計測の後の周波数、上限(最大周波数と違うときだけ `cap`)である。
- 窓の行: シングルは `単位数u ns_per_unit ns cpu… umax 最長の単位(ms、小数 2 桁)`。マルチは、`w` の後にワーカーごとの単位数の最小と最大を足す。
- `rej N` の行は、採用しなかった回ごとに 1 行出す(N は回の番号。続けて、待ち、確認の値、ばらつき、本計測の窓の `ns_per_unit`、タッチがあれば回数)。採用した回の窓の行の後に置く。上の例は、3 回測って 2 回目を採用した場合である。
- 診断のクラスタ別の行は、`7.05ms` の後に ` max 7.31ms` を足す。ほかの診断の行は、PR-B の書式のままである。
- 例の数値は、書式を示すためのもので、実測ではない。

### P3.16 `analyze_summary.py`

- 0.2.0、0.2.1、0.2.2、0.3.0 の 4 つの書式を読めるようにする。保存済みの記録(`docs/measurements/`)を読めなくしない。
- PR-A で `--strict` を足す。`!!`(欠け、切れ、文字数の不一致)が 1 つでもあるか、テストが 1 本も読めない実行があるか、解釈できない行があれば、終了コードを 1 にする。解釈できない行は `??` を付けて出す。
- PR-B で、`input` の行、`INTERRUPTED`、`touches`、候補 (a)(b) の集計を足す。
- PR-D で、新しい書式を足す。あわせて次を出す。
  - テストごと: 窓ごとの単位数と `ns_per_unit`、ばらつき、窓を追って下がる傾向があるか(V4)、ウォームアップの窓と本計測の窓の CPU(V15)、採用しなかった回。
  - 実行をまたいだ集計: テストと段階ごとの、スコアの平均、標準偏差(n − 1)、変動係数。3 回以上あるときに出す(V14。目標はピーク 2%、持続 3%)。「回復せず」の付いた回を除いた値も並べる(K1)。
  - 開始のタッチ直後のクロックと基準クロックの比、「回復せず」の数、タッチの数(V5 (d))。
  - クラスタ別の `max` と、10 ms との比較(V21)。

### P3.17 画面(Android / iOS)

両方の OS で、同じ意味の表示にする。core の JSON と進捗の値を表示するだけにする。

| 場所 | 変更 | PR |
|---|---|---|
| 結果 | 無効の理由を分けて出す: 中止された / 計測中にアプリが前面を離れたため無効 / チェックサム不一致 | B |
| 進捗 | `start_wait` のとき: 「開始待ち: 端末を置いて、画面に触れないでください(残り N 秒)」。N は `(wait_limit_ms − wait_ms)` から求める | D |
| 進捗 | 回復待ち: 「回復待ち N 秒(最大 M 秒)」。M は `wait_limit_ms` から求める。「(最大 60 秒)」という固定の文言(`strings.xml:18`、`ContentView.swift:53`)をやめる | D |
| 進捗 | 「ウォームアップ」「本計測」の区別をやめる | D |
| 開始前の説明 | 所要時間の文言(今は「1〜5 分」)を、CLI で測った時間に合わせて直す | D |
| テストのカード | `median 1.333 ms/unit / threads 1 / 2.51 GHz / ±0.1%` の形にする。測り直し、待ち、「回復せず」、無効の表示は今と同じ | D |
| 内訳 | 「各回の内訳」を「窓ごとの内訳」にする。行は `#1 150 units 1.333 ms/unit cpu7`。マルチは、ワーカーごとの単位数の最小と最大を足す | D |
| タッチの数 | 画面には出さない(要約と JSON にだけ出す) | — |

Swift の `Decodable` は、キーがないと復号に失敗する。足すキーは省略可能(`Optional`)にし、消すキーはモデルから消す(今は `medianNs`、`runsNs`、`Config.warmupRuns`、`Config.measuredRuns` が必須になっている)。

### P3.18 CLI と CI

- CLI に `--start-wait-ms N` を足す(PR-D)。`RunPolicy::start_wait_ms` を置き換える。既定は 10000 のままにする。
- `core/tests/CMakeLists.txt` の `cli_smoke` と `cli_diagnostics` は、`--start-wait-ms 100` を付けて実行する。`PASS_REGULAR_EXPRESSION` は、新しい JSON のキーに合わせる。
- `core-test.yml` の「Run full benchmark」は、指定を付けない。既定の手順(開始待ち 10 秒)を、CI で 1 回は通す。
- 参考: 今の `ctest` は、手元(GCC 13.3.0)で 80 秒、macOS のランナーで 183 秒かかった【実行】。macOS で `cli_smoke` が 55 秒かかったのは、回復待ちが長引いたためと考えられる【推測】。新しい手順では、回復待ちがテストごとに最大 8 秒になる。

### P3.19 暫定の基準値(PR-D、J11)

1. 実装が済んだら、開発用の環境で `./build/mobilebench_cli --quiet` を 3 回実行する。
2. テストごとに、ピーク段階の `median_ns_per_unit` を 3 つ集め、中央値を取る。
3. 有効数字 2 桁に丸めて、`reference_times.h` に入れる(定数の名前は `kCpuSingleSieveNsPerUnit` のようにする)。
4. コメントに、環境(x86_64、コンパイラ、論理コア数)、日付、「暫定値。基準機の実測値ではない」を書く。

共有の環境なので、値は実行のたびに大きく揺れる(§1.6(a))。暫定値の精度は求めない。

### P3.20 版を上げるときに直す場所

1. `core/include/mobilebench/version.h`(値と、履歴のコメント)。
2. `android/app/build.gradle.kts` の `versionName`。
3. `ios/project.yml` の `MARKETING_VERSION`。
4. `schema_version` を上げるときは、`result_json.cpp`(値と、スキーマの説明のコメント)、`BenchmarkResult.kt` と `BenchmarkResult.swift` の冒頭のコメント、ユニットテストの中の `"schema_version":N` の文字列。

PR-A の検査が、1〜3 の一致を見る。

### P3.21 試作で確かめたこと【実行】

2026-11-16 に、開発用の環境(x86_64、論理 4 コア、共有の仮想マシン)で確かめた。コードは作業用の場所に置き、リポジトリには入れていない。端末の値ではない。

**(1) 窓の数え方(作った時刻の列。実時間を使わない)**

P3.2 の数え方を、決めた時刻の列に当てて、「計測したスループット ÷ 真のスループット」を求めた。窓は 200 ms × 6 で、値は本計測の 5 窓のものである。

| 模擬した状況 | 中央値 | 最小〜最大 |
|---|---|---|
| 基準機の単位時間(篩 上限 100 万: 4.12 / 1.46 / 1.33 ms)。8 本が 8 コア | 1.0000 | 1.0000〜1.0000 |
| 基準機の単位時間(行列積の片: 7.05 / 0.72 / 0.67 ms) | 1.0000 | 1.0000〜1.0000 |
| 8 本が 6 コアを分け合う(各ワーカーは 6 ms 動いて 2 ms 待つ)。単位 1.08 ms | 1.0002 | 1.0002〜1.0002 |
| 同(12 ms 動いて 4 ms 待つ)。単位 0.90 ms | 0.9996 | 0.9981〜1.0029 |
| 16 本が 4 コアを分け合う(4 ms 動いて 12 ms 待つ)。単位 1.5 ms | 1.0036 | 0.9995〜1.0039 |
| 1 単位(250 ms)が窓(200 ms)より長い | 1.0000 | 0.0000〜1.0000 |

- コアより多いワーカーが動いても、偏りは 0.4% 以内だった。この模擬は、スレッドがコアの間を移動しない形である。移動する場合は、§1.6(b) のシミュレーションが扱っている。
- 単位が窓より長いと、単位が 1 つも終わらない窓ができる(P3.2 の 6 で無効にする)。

**(2) 実スレッドでの動作**

| | ワーカー 1 本 | 4 本(コアと同じ数) | 8 本(コアの 2 倍) |
|---|---|---|---|
| 篩 上限 100 万: 1 単位あたりの時間(5 窓の中央値) | 915,830 ns | 229,086 ns | 224,447 ns |
| 行列積の片: 同上 | 531,883 ns | 129,337 ns | 131,438 ns |
| 5 窓のばらつき(篩 / 行列積) | 4.5% / 12.3% | 4.0% / 32.5% | 9.8% / 6.6% |

- ワーカーをコアの 2 倍にしても、1 単位あたりの時間は 4 本のときとほぼ同じだった(差は、篩で 2.0%、行列積で 1.6%)。
- 窓ごとのばらつきは、共有の環境のため大きい。ユニットテストで、実時間のばらつきを合否に使わない(P2.4)。
- コアより多いワーカーでは、いちばん長い単位が 8.5〜16.9 ms になった(単位そのものは 0.5〜0.9 ms)。順番待ちの時間が入るためである。HONOR 端末(8 本が 6 コアを分け合う)のマルチでも、`umax` は順番待ちを含む値になる。片の最大時間(規則 4)は、コアに固定して測るクラスタ別の診断で見る。

**(3) 行列積の片の重さ(n=576。25 回の中央値)**

| | 普通の片 | 行ブロックの最初の片 | 行ブロックの最後の片 | 片 0 | 最後の片 |
|---|---|---|---|---|---|
| 行ブロックごとに置く(clang 18.1.3) | 0.50〜0.51 ms | +4.2〜5.2% | +7.6〜7.8% | +5.2〜7.4% | +7.6〜8.7% |
| 行列積ごとに置く(clang 18.1.3) | 0.49〜0.51 ms | +4.1〜6.8% | −0.3〜+0.2% | +26.2〜26.7% | +80.9〜85.4% |
| 行ブロックごとに置く(GCC 13.3.0) | 1.03 ms | +3.1% | +3.4% | +3.9% | +3.6% |
| 行列積ごとに置く(GCC 13.3.0) | 1.03 ms | +4.3% | −0.1% | +23.0% | +50.9% |

- 単独で測ると、行 64 本の照合は 0.046〜0.059 ms、全体の照合は 0.35〜0.40 ms、行 64 本のゼロ埋めは 0.010〜0.013 ms、全体のゼロ埋めは 0.082〜0.083 ms だった。
- 片に分けた版の行列積 1 回ぶんの合計は、今の単位(1 回)に比べて、clang で 5〜6% 短く、GCC ではほぼ同じ(0〜1% 短い)だった。原因は確かめていない【推測: ゼロ埋めの直後に同じ行を使うこと、関数の分け方による生成コードの違い】。単位の定義が変わるので、基準機の値は測り直す(P3.4)。
- 照合(行列積 1 回が完成するごと)は、どの版でも通った。

---

## P4. PR ごとの手順とプロンプト

プロンプトは、新しいセッションにそのまま渡せる形にしてある。J9 を見送る場合など、P0 の推奨と違う案を選んだときは、該当する行を直して渡す。

### P4.1 PR-A: CI の検査を足す

**目的**: フェーズ 1 の変更で起こりうる食い違いを、実機に入れる前に CI で止める。フェーズ 0 の残りのうち、CI だけで済むものを片づける(P1.5)。アプリと core の動作は変えない。

**前提**: `main` に、PR #4 と本書が入っている。

**版**: `benchmark_version`(0.2.1)と `schema_version`(6)は変えない。

**作業**

1. **Xcode の固定**(J8): `ios.yml` のランナーを `macos-26` にする。`sudo xcode-select -s /Applications/Xcode_26.6.app` で版を選ぶ。`xcodebuild -version` の出力を、想定の値(`Xcode 26.6`、`Build version 17F113`)と比べ、違えば失敗させる。`clang --version` もログに出す。想定の値は、ワークフローの冒頭の `env` に置く。作業の時点で、イメージの Xcode 26.6 のビルド番号が変わっていたら、その値に合わせ、PR の説明に書く。Xcode 26.6 がイメージからなくなっていたら、作業を止めて報告する。
2. **3 か所のバージョンの一致**: `.github/scripts/check_versions.py` を足す。`version.h` の `kBenchmarkVersion`、`build.gradle.kts` の `versionName`、`project.yml` の `MARKETING_VERSION` が同じであること、`version.h` の履歴のコメントにその版の行があることを検査する。3 つのワークフローすべてで実行する。
3. **JNI の照合**(J7 の 1): `.github/scripts/check_jni.py` を足す。`NativeBench.kt` の `external fun` ごとに、`jni_bridge.cpp` に `Java_com_mobilebench_app_bench_NativeLib_<名前>` があること。`jni_bridge.cpp` の `GetMethodID(…, "<名前>", "<シグネチャ>")` ごとに、Kotlin の `fun interface` の宣言から求めたシグネチャと一致すること。`android.yml` で実行する。
4. **Kotlin のパーサのテスト**(J7 の 2): `android.yml` で、ホスト向けに core の CLI をビルドし、`--only cpu.single.sieve --diagnostics --quiet` の JSON を作る。`testDebugUnitTest` で、その JSON を `BenchmarkResult.parse` に読ませる。JVM のテストで `org.json` を使うための依存を `testImplementation` に足す(版は、作業の時点の安定版を確かめて決め、PR の説明に書く)。検査するのは、例外なく読めること、`tests` と `summaryParts` と `sweep` が空でないことである。JSON のパスは環境変数で渡し、未設定ならテストを飛ばす。
5. **Swift のパーサの検査**(J7 の 3): `ios.yml` で、すでにビルドしている `build-core/mobilebench_cli` で同じ JSON を作る。`swiftc` で `ios/MobileBench/Model/BenchmarkResult.swift` と、検査用の `main.swift`(`.github/scripts/swift_parse_check/` に置く)をコンパイルし、JSON を読ませる。
6. **`analyze_summary.py` の検査**(J7 の 4): `--strict` を足す(P3.16)。読み飛ばす行(`columns:`、掃引の表など)は、既知の行として扱う。`core-test.yml` で次を行う。
   - 保存済みの記録を、ディレクトリごとに読ませる。0.2.1 の記録(`2026-11-15-*`)は `--strict` で通ること。0.2.0 の記録(`2026-11-12-*`。途中で切れている)は、`--strict` なしで例外が出ないこと。
   - CLI の JSON(診断モードあり)から `summary_parts` を取り出してファイルにし、`--strict` で読ませる。
   - `core-test.yml` の `paths` に、`docs/measurements/tools/**` と `.github/scripts/**` を足す。
7. **カーネルの位置**(C8、J9):
   - `android.yml` で、arm64-v8a の `libmobilebench_jni.so`(シンボルの残っているもの)から、カーネルの関数(名前に `count_primes_segmented`、`matmul_blocked` を含むもの。名前の一覧はスクリプトの冒頭に置き、後の PR が足せるようにする)について、アドレス、大きさ、アドレスを 64 で割った余り、関数の中の後ろ向きの分岐(ループ)の飛び先のアドレスと 64 で割った余り、逆アセンブルを、テキストに出す。APK に入る `.so` と `.text` の内容が同じことも確かめる。成果物(`kernel-layout`)として上げ、要点をログにも出す。道具は NDK の `llvm-nm`、`llvm-objdump`、`llvm-readelf` を使う。スクリプトは `.github/scripts/kernel_layout.py` に置く。
   - 一度だけの比較(J9): 新しいワークフロー `kernel-layout.yml`(手動の実行と、自身の変更の push で動く)で、コミット `8145938` と `7dcf4a1` をビルドし直す。実機で使った APK(Android のワークフローの実行 `42952127373` = build 12、`43670028049` = build 15 の成果物 `MobileBench-debug-apk`)の `.so` と、`.text` を比べる。一致したかどうかと、両方のカーネルの位置を出す。結果のテキストを `docs/measurements/<作業日>-kernel-layout/` に保存し、`docs/measurements/README.md` の一覧に足す。原因の判断はしない。事実だけを書く。

**変更するファイル**

- `.github/workflows/ios.yml`、`android.yml`、`core-test.yml`、(新規)`kernel-layout.yml`
- (新規)`.github/scripts/check_versions.py`、`check_jni.py`、`kernel_layout.py`、`swift_parse_check/main.swift`
- `android/app/build.gradle.kts`(テスト用の依存)、(新規)`android/app/src/test/java/com/mobilebench/app/bench/BenchmarkResultParseTest.kt`
- `docs/measurements/tools/analyze_summary.py`、`docs/measurements/README.md`、(新規)`docs/measurements/<作業日>-kernel-layout/`
- `README.md`(GitHub Actions の表)、`docs/DESIGN.md`(§8 フェーズ 0 の該当する行に実装した旨、§6 の C6 と C8 の結果)、`CLAUDE.md`(「ビルドとテスト」に、CI の検査を 1〜2 行)

**確かめ方**

- 足したスクリプトを手元で実行する。わざと食い違わせて、失敗することも確かめる(確かめたら戻す)。
- 3 つのワークフローが成功する。`kernel-layout.yml` が最後まで動き、結果が出る。

**マージ後の実機**: 不要。

**プロンプト**

```text
MobileBench(リポジトリ Zuikakulove/mobile-bench)の作業です。
docs/PHASE1_PLAN.md の PR-A(CI の検査を足す)を実装してください。

■ 最初に読むもの
1. CLAUDE.md(全部)
2. docs/PHASE1_PLAN.md の P0.1 の J7〜J9、P1.5、P2(全部)、P3.16、P3.20、P4.1
3. docs/DESIGN.md の §1.5 の 2・3・9、§1.8 (4)、§4-Q9 の「CI の修正」、§6 の C6 と C8

■ 前提
main に、PR #4(docs/DESIGN.md に §1.8 がある)と docs/PHASE1_PLAN.md が入っていること。
入っていなければ、作業を始めずに報告してください。

■ やること
docs/PHASE1_PLAN.md P4.1 の「作業」の 1〜7 です。
CI とテストの検査を足すだけで、アプリと core の動作は変えません。

■ やらないこと
- core/src と core/include の変更(計測区間、手順、JSON、要約の書式を変えない)
- benchmark_version と schema_version の変更
- .github/workflows/android.yml のファイル名の変更
- iOS のフラグの検査(フェーズ 2 で行う)

■ 確かめ方
- 足したスクリプトは手元で実行し、わざと食い違わせた場合に失敗することも確かめてください(確かめたら戻す)。
- push の後、Core tests、Android、iOS の 3 つのワークフローが成功することを確かめてください。
- 作業 7 の kernel-layout のワークフローは、結果のテキストを確かめて、docs/measurements/ に保存してください。
  原因の判断は書かず、一致したかどうかと位置の事実だけを書いてください。

■ push と PR
- この依頼が、push と PR 作成の指示です。作業ブランチに push し、main への PR を作成してください。
- マージはしないでください。
- PR の説明は docs/PHASE1_PLAN.md P2.8 の 6 項目で書いてください。実機の確認は不要です。

■ 迷ったとき
docs/PHASE1_PLAN.md P2.7 に従ってください。外から見える点で決まらないものは、進めずに私に尋ねてください。
外部の仕様(GitHub のランナー、Xcode、Gradle の依存の版など)は、記憶で書かず、作業の時点の公式の資料か CI のログで確かめてください。
```

### P4.2 PR-B: 0.2.2(フェーズ 0 の残り、タッチの記録、クロックの候補)

**目的**: 計測に影響しない変更を、固定時間方式への切り替えの前に入れる。V14 の前提になる「前面を離れたら無効」を入れる。V22 の候補のデータを取れるようにする。

**前提**: PR-A がマージされている。

**版**: `benchmark_version` を **0.2.2** に、`schema_version` を **7** に上げる(J2)。計測区間、単位、手順、基準値、フラグは変えない。

**作業**

1. **core**
   - `Runner::interrupt`(P3.10)、`Runner::note_touch`(P3.11)、呼び出しスレッドの設定(P3.12)。
   - 診断モードのクロックの比較を、候補 (a)(b) に入れ替える(P3.13 (1))。`busy_spin_counted` を消し、共通の時刻まで負荷をかける関数を足す。
   - JSON を schema 7 にする(P3.14)。要約に P3.15 の「PR-B で足すもの」を足す。
   - 版を 0.2.2 にする(P3.20)。
2. **Android**: JNI の関数を 2 つ足す(中断、タッチ)。`NativeBench`、`BenchmarkViewModel`、`MainActivity`(`onStop`、`dispatchTouchEvent`)、パーサ(`interrupted`、`interruption`)、無効の理由の表示(P3.17)、`versionName`。PR-A のパーサのテストに、足したキーの検査を足す。
3. **iOS**: ブリッジにメソッドを 2 つ足す。ViewModel(バックグラウンドの通知、QoS の指定を消す)、タッチを見る部品、パーサ(足すキーは省略可能にする)、無効の理由の表示、`MARKETING_VERSION`。
4. **`analyze_summary.py`**(P3.16 の PR-B の分)。
5. **ユニットテスト**
   - 中断: 進捗の通知の中から `interrupt("left_foreground")` を呼ぶ。`interrupted` が真、`valid` が偽、`cancelled` が偽、要約に `INTERRUPTED(left_foreground)`、JSON に `"interrupted":true`。
   - タッチ: 計測中に 2 回呼ぶ。`touches` が 2、そのテストの `touches` が 2。`run()` の外で呼んだ分は数えない。
   - クロックの比較: 現行の値と候補 (b) の値が記録される。最小 ≤ 平均 ≤ 最大。取り消しで途中で止まる。
   - 要約: 足した行の書式。大きい結果でも、パートが 8,000 バイト以内に収まる。
   - JSON: `"schema_version":7` と、足したキー。
6. **文書**
   - `README.md`: 診断モードのクロックの比較の説明、前面を離れたときの扱い、タッチの記録。
   - `docs/DESIGN.md`: §5.2(schema 7 の内容。固定時間方式は 8 にする、と改める)。§5.5(診断モードの記録を候補に入れ替えたこと)。§6 に V25(計測中に前面を離れたら無効になるか。Android)と V26(画面へのタッチの記録)を足す。§6 の V22 の行に、候補を 0.2.2 で足した旨を書く。§8 フェーズ 0 の該当する行に実装した旨。「保留: iOS 実機」に、前面とタッチの確認を足す(J12)。§7 の「確認できなかったもの」のタッチの検出の行を、実装に合わせて直す。
   - `CLAUDE.md`: 「決定に実装が追いついていない点」の、診断モードの比較の行。

**変更するファイル**

- core: `include/mobilebench/runner.h`、`result.h`、`telemetry.h`、`version.h`、`src/runner.cpp`、`result_json.cpp`、`summary.cpp`、`diagnostics.cpp`、`diagnostics.h`、`clock_probe.cpp`、`clock_probe.h`、`tests/test_runner.cpp`、`tests/test_summary.cpp`
- Android: `app/src/main/cpp/jni_bridge.cpp`、`bench/NativeBench.kt`、`bench/BenchmarkResult.kt`、`BenchmarkViewModel.kt`、`MainActivity.kt`、`ui/BenchmarkScreen.kt`、`res/values/strings.xml`、`app/build.gradle.kts`、`src/test/…/BenchmarkResultParseTest.kt`
- iOS: `Bridge/MBBenchmarkBridge.h`、`MBBenchmarkBridge.mm`、`Model/BenchmarkViewModel.swift`、`Model/BenchmarkResult.swift`、`UI/ContentView.swift`、(新規)タッチを見る部品、`project.yml`
- 文書と道具: `docs/measurements/tools/analyze_summary.py`、`README.md`、`docs/DESIGN.md`、`CLAUDE.md`

**確かめ方**

- 手元の `ctest`。3 つのワークフロー(PR-A の検査を含む)。
- 足した JNI の関数の名前と、Kotlin の宣言を、PR の説明に並べて書く。

**マージ後の実機**: 要。P5.1。

**プロンプト**

```text
MobileBench(リポジトリ Zuikakulove/mobile-bench)の作業です。
docs/PHASE1_PLAN.md の PR-B(0.2.2)を実装してください。

■ 最初に読むもの
1. CLAUDE.md(全部)
2. docs/PHASE1_PLAN.md の P0.1 の J2・J6・J12、P1.3〜P1.5、P2(全部)、P3.10〜P3.17、P3.20、P4.2、P5.1
3. docs/DESIGN.md の §1.5 の 4・5、§1.8 (2)(3)、§4-Q3、§4-Q7 の 2・3、§5.2、§5.3、§5.5 の
   「状態を確かめるクロックの求め方」と「記録と表示」、§6 の V22、§8 のフェーズ 0

■ 前提
main に、docs/PHASE1_PLAN.md の PR-A(.github/scripts/check_versions.py などの検査)が入っていること。
入っていなければ、作業を始めずに報告してください。

■ やること
docs/PHASE1_PLAN.md P4.2 の「作業」の 1〜6 です。要点は次のとおりです。
- 計測中にアプリが前面を離れたら、計測を中止して無効にする(core、Android、iOS)
- 計測中の画面へのタッチを記録する(判定には使わない)
- 呼び出しスレッドの優先度の設定を、core の Runner::run の入口に寄せる
- 診断モードのクロックの比較を、候補 (a)(b) に入れ替える(判定には使わない)
- benchmark_version を 0.2.2、schema_version を 7 にする

■ やらないこと
- 計測区間、処理単位、ピークと持続の手順、判定の値、基準値、コンパイルフラグの変更
- 固定時間方式に関わる変更(PR-C と PR-D で行う)
- docs/PHASE1_PLAN.md P1.4 の項目(警告の表示など)
- iOS の署名や実機向けの作業

■ 確かめ方
- core を変えたら、cmake でビルドして ctest を通してください。
- 時間に依存するテストは、docs/PHASE1_PLAN.md P2.4 の書き方にしてください。
- Android と iOS は手元でビルドできません。push の後、Core tests、Android、iOS の 3 つのワークフローが
  成功することを確かめ、失敗したら直してください。
- JNI の関数名は実行時にしか検査されません。Kotlin の宣言と jni_bridge.cpp の関数名を、PR の説明に並べて書いてください。
- Android の onStop と構成変更の見分け方、iOS の通知の名前は、公式の資料で確かめてください。確かめられなければ、
  PR の説明に「未確認」と書いてください。

■ push と PR
- この依頼が、push と PR 作成の指示です。作業ブランチに push し、main への PR を作成してください。
- マージはしないでください。
- PR の説明は docs/PHASE1_PLAN.md P2.8 の 6 項目で書いてください。
  「マージ後に実機で確かめること」には、P5.1 の手順を写してください。

■ 迷ったとき
docs/PHASE1_PLAN.md P2.7 に従ってください。計画と違う実装にした点は、PR の説明と docs/PHASE1_PLAN.md P7 に書いてください。
```

### P4.3 PR-C: 固定時間方式の計測の部品

**目的**: 窓を止めずに続ける計測と、新しい単位を、ユニットテストつきで先に入れる。スコア対象の手順(`Runner`、既定スイート、診断モード)は、まだ現行のままにする。

**前提**: PR-B がマージされている(実機の確認は、終わっていなくてよい)。

**版**: 変えない(0.2.2、schema 7)。スコア対象の計測と JSON は変わらない。

**作業**

1. `core/src/window_recorder.h`: `WorkerWindow`、`WindowRecorder`、窓ごとの集計の関数(P3.2)。
2. `core/src/tests/timed_kernels.h`、`timed_kernels.cpp`: `TimedKernel`、`SieveTimedKernel`、`MatmulPieceKernel`、`matmul_piece`(P3.3〜P3.5)。`matmul_checksum_rows` は `cpu_matmul.cpp` に足す(既存の関数の本体は変えない)。
3. `core/src/tests/timed_unit_test.h`: `TimedUnitTest`(P3.6)。
4. `core/CMakeLists.txt` と `core/tests/CMakeLists.txt` に、足したソースを入れる。
5. ユニットテスト(新規 `core/tests/test_windows.cpp`)
   - **数え方(作った時刻の列)**: 等速の単位で、窓ごとの単位数と実時間が期待どおりになる。境目ちょうどで終わった単位は、その窓に入る。1 つの単位が複数の境目をまたぐと、0 の窓ができる。最後の境目で `false` が返る。
   - **偏りのないこと(作った時刻の列)**: P3.21 (1) の表の状況を再現する。速さの違うワーカー(基準機の単位時間)、6 コアを 8 本で分け合う場合、4 コアを 16 本で分け合う場合に、計測したスループット ÷ 真のスループットが 1 ± 0.005 に入る。単位が窓より長いと、0 の窓ができる。
   - **カーネル**: 篩 上限 100 万の素数が 78,498 個。`MatmulPieceKernel`(n=128、192)で、全部の片を実行した後の出力が `matmul_blocked` の結果と一致し、照合が通る。2 回続けても通る。途中で止めて `finish` を呼ぶと通る。正解を違う値にすると、`checksum_ok` が偽になる。
   - **`TimedUnitTest`(実スレッド。窓は 20〜30 ms、ウォームアップ 1 + 本計測 3)**: 窓の数が合う。各ワーカーの単位数の合計が、偽のカーネルが数えた数と一致する。照合が通る。シングルとマルチ(2 本)。コアより多いワーカー(論理コア数 × 2 + 1 本)で、同じ不変条件が成り立つ。速さの違う偽のカーネル(ワーカー 0 は 200 µs、ワーカー 1 は 500 µs の間、時計を見ながら待つ)で、スループットが期待値の ±20% に入る。
6. 本書の P7(計画との差があれば)。

**変更するファイル**

- (新規)`core/src/window_recorder.h`、`core/src/tests/timed_kernels.h`、`core/src/tests/timed_kernels.cpp`、`core/src/tests/timed_unit_test.h`、`core/tests/test_windows.cpp`
- `core/src/tests/cpu_kernels.h`、`core/src/tests/cpu_matmul.cpp`(関数を足すだけ)、`core/CMakeLists.txt`、`core/tests/CMakeLists.txt`
- `.github/scripts/kernel_layout.py`(名前の一覧に `matmul_piece` を足す)

**確かめ方**

- 手元の `ctest`。既存のユニットテストは、変更なしで通ること。
- `mobilebench_cli` の JSON の形が変わっていないこと(PR-A のパーサの検査が通る)。
- 3 つのワークフロー。足したテストを、手元で 10 回続けて実行し、1 回も失敗しないこと。

**マージ後の実機**: 不要。

**プロンプト**

```text
MobileBench(リポジトリ Zuikakulove/mobile-bench)の作業です。
docs/PHASE1_PLAN.md の PR-C(固定時間方式の計測の部品)を実装してください。

■ 最初に読むもの
1. CLAUDE.md(全部)
2. docs/PHASE1_PLAN.md の P0.1 の J4、P2(全部。特に P2.4 と P2.6)、P3.1〜P3.6、P3.21、P4.3
3. docs/DESIGN.md の §1.6(b)、§1.8 (1)(3)、§4-Q5(規則 1〜6 と「判断」)、§5.1
4. docs/measurements/tools/simulate_units.py の fixed_time_continuous(数え方の手本)
5. core/src/tests/unit_based_test.h、cpu_matmul.cpp、cpu_sieve.cpp、core/src/worker_pool.*(現行の実装)

■ 前提
main に、docs/PHASE1_PLAN.md の PR-B(benchmark_version 0.2.2)が入っていること。
入っていなければ、作業を始めずに報告してください。

■ やること
docs/PHASE1_PLAN.md P4.3 の「作業」の 1〜6 です。
新しい部品(窓の数え方、単位、TimedUnitTest)とユニットテストを足します。
既定スイート、Runner、診断モードからは、まだ使いません。

■ やらないこと
- Runner、registry、診断モード、JSON、要約の変更(PR-D で行う)
- 既存のカーネルの関数(count_primes_segmented、matmul_blocked、matmul_checksum)と、
  UnitBasedTest の本体の書き換え
- 内側のループの書き方の変更、ループへの指定、NEON の組み込み関数(フェーズ 2 で行う)
- benchmark_version と schema_version の変更
- スレッドをコアに固定すること

■ 確かめ方
- cmake でビルドして ctest を通してください。既存のユニットテストは、変更なしで通ること。
- 足したテストは、docs/PHASE1_PLAN.md P2.4 の書き方にしてください。
  数え方は、作った時刻の列で確かめます。実スレッドのテストは、不変条件と広い許容差だけにします。
- 足したテストを、手元で 10 回続けて実行し、1 回も失敗しないことを確かめてください。
- push の後、Core tests、Android、iOS の 3 つのワークフローが成功することを確かめてください
  (iOS のワークフローは、macOS のランナーで同じユニットテストを実行します)。

■ push と PR
- この依頼が、push と PR 作成の指示です。作業ブランチに push し、main への PR を作成してください。
- マージはしないでください。
- PR の説明は docs/PHASE1_PLAN.md P2.8 の 6 項目で書いてください。実機の確認は不要です。

■ 迷ったとき
docs/PHASE1_PLAN.md P2.7 に従ってください。計画と違う実装にした点は、PR の説明と docs/PHASE1_PLAN.md P7 に書いてください。
```

### P4.4 PR-D: 0.3.0(固定時間方式への切り替え)

**目的**: スコア対象の計測を固定時間方式に切り替え、ピーク段階の手順を §5.5 のとおりにする。計測に影響する変更を、この 1 本にまとめる(J1)。

**前提**: PR-B と PR-C がマージされている。PR-B の実機の確認(P5.1)で問題が出ていれば、直す PR が先にマージされている(K5)。

**版**: `benchmark_version` を **0.3.0** に、`schema_version` を **8** に上げる。

**作業**(コミットを分ける目安でもある)

1. **core の計測**
   - `benchmark_test.h`、`runner.h`、`runner.cpp` を P3.7〜P3.9 のとおりにする。
   - `registry.cpp`: 単位と窓(P3.1)。16 単位の定数と、その説明のコメントを消す(D9)。`category` を替える(J3)。
   - `TimedUnitTest` を `BenchmarkTest` の実装にする。`unit_based_test.h` と、現行の `UnitKernel`、`SieveKernel`、`MatmulKernel` を消す。診断モードの `matmul128` のために、今の `MatmulKernel::run_unit` と同じ処理を、`TimedKernel` の形で残す(P3.13 (2))。
   - `reference_times.h` の形式を替える(値は、作業 8 で入れる)。
2. **診断モード**(P3.13 (2)(3))。
3. **JSON、要約、`analyze_summary.py`**(P3.14 の schema 8、P3.15 の PR-D の書式、P3.16)。
4. **ユニットテスト**
   - `Runner`(窓ごとの値を自分で返す偽のテストで): 中央値とスコアの式。ウォームアップの窓を使わないこと。照合の失敗で無効。本計測の窓に 0 があれば無効。ばらつきと安定。測り直し(1 回目が不安定、2 回目が安定なら、回数は 2、採用は 2 回目、`attempt_log` は 2 件)。3 回とも不安定なら、ばらつきが最小の回を採用して「不安定」。取り消し。中断。回ごとのタッチの数。
   - ピークの手順: 開始待ち(テストでは短い値)と、進捗の `phase` の順(`prepare` → `start_wait` → `prepare` → `peak` → `sustained`)。`recovered_ratio = 0` なら、確認は 1 回で、待ちは `min_rest_ms`。`recovered_ratio = 1e9` なら、確認は 3 回で、待ちは `min_rest_ms + 2 × check_interval_ms`、「回復せず」。合計の上限(テスト 3 本、上限を確かめ直し 3 回ぶんにする。1 本目は 2 回、2 本目は 1 回で打ち切り、3 本目は確認 1 回だけ)。持続だけのときは、開始待ちがない。
   - 既定スイート: 4 本、ID、`category`、`window_ms = 200`、基準値が正。
   - JSON: schema 8 のキーがあり、消したキーがない。
   - 要約: 書式。大きい結果(テスト 9 本 × 2 段階、診断あり、論理コア 16 個、採用しなかった回あり)で、パートが 8,000 バイト以内に収まり、まとまりが切れない。
   - 診断モード: クラスタ別(`matmul576p`、`max_ms_per_unit`)、行列サイズ掃引、ステップ数。
5. **CLI と ctest**(P3.18)。
6. **Android**: JNI の進捗のシグネチャ(P3.7)、`NativeBench.kt`、ViewModel、パーサ、画面と文言(P3.17)、パーサのテスト、`versionName`。
7. **iOS**: ブリッジの進捗の型、ViewModel、パーサ、画面と文言、`MARKETING_VERSION`。
8. **暫定の基準値と版**(P3.19、P3.20)。
9. **文書**
   - `README.md`: 計測ルール、テストの表、2 段階の計測の説明、JSON の例、計測の内訳、診断モード、テストの追加方法、基準時間の差し替え方。拡張予定の表は、§4-Q4 と §8 に合わせる。「モバイルは両 OS とも clang 系なので条件は揃っています」は、未検証であることが分かる書き方に直す(§1.4)。
   - `docs/DESIGN.md`: 冒頭の経緯の表に 1 行。§5.1、§5.2、§5.5 に、0.3.0 で実装した旨と、案から変えた点(本書の P0.3 と P7)。§8 フェーズ 1 の状態。§8 フェーズ 6 に、J13 の 2 項目。
   - `CLAUDE.md`: 「主要ディレクトリ」、「ビルドとテスト」、「テストを追加する手順」。「決定に実装が追いついていない点」から、解消したもの(固定仕事量方式、ピークの手順)を消す。基準値が暫定であること、クロックの求め方が現行のままであることは残す。

**大きすぎる場合の分け方**: 作業 1〜5 と、作業 6・7 のうちビルドとパーサの検査が通る最小限までを PR-D とし、画面の文言と README を PR-D2 に分けてよい。計測は変えないので、版は 0.3.0 のままにする。PR-D2 がマージされるまで、実機の確認(P5.2)は始めない。分けた場合は、PR の説明に書く。

**変更するファイル**

- core: `include/mobilebench/benchmark_test.h`、`runner.h`、`result.h`、`telemetry.h`、`reference_times.h`、`version.h`、`src/runner.cpp`、`registry.cpp`、`stats.cpp`、`result_json.cpp`、`summary.cpp`、`diagnostics.cpp`、`diagnostics.h`、`src/tests/`(`timed_*`、`cpu_kernels.h`、`cpu_matmul.cpp`、`cpu_sieve.cpp`。`unit_based_test.h` は削除)、`tests/`(`test_runner.cpp`、`test_summary.cpp`、`test_stats_json.cpp`、`test_kernels.cpp`、`test_windows.cpp`、`CMakeLists.txt`)、`tools/cli_main.cpp`、`CMakeLists.txt`
- Android: `jni_bridge.cpp`、`NativeBench.kt`、`BenchmarkResult.kt`、`BenchmarkViewModel.kt`、`BenchmarkScreen.kt`、`strings.xml`、`build.gradle.kts`、パーサのテスト
- iOS: `MBBenchmarkBridge.h`、`MBBenchmarkBridge.mm`、`BenchmarkResult.swift`、`BenchmarkViewModel.swift`、`ContentView.swift`、`project.yml`
- CI と道具: `.github/workflows/core-test.yml`(必要なら)、`.github/scripts/kernel_layout.py`(名前の一覧)、`docs/measurements/tools/analyze_summary.py`、`docs/measurements/README.md`
- 文書: `README.md`、`docs/DESIGN.md`、`CLAUDE.md`、本書の P7

**確かめ方**

- 手元の `ctest`。`./build/mobilebench_cli --quiet` を 3 回実行して、4 テストとも有効であること、要約が `analyze_summary.py --strict` で読めることを確かめる。
- 3 つのワークフロー(JNI の照合、パーサの検査、要約の解析の検査を含む)。
- JNI の進捗のシグネチャと、Kotlin の宣言を、PR の説明に並べて書く。
- CLI で測った所要時間(診断モードのあり・なし)を、PR の説明に書く。

**マージ後の実機**: 要。P5.2。

**プロンプト**

```text
MobileBench(リポジトリ Zuikakulove/mobile-bench)の作業です。
docs/PHASE1_PLAN.md の PR-D(0.3.0。固定時間方式への切り替え)を実装してください。
フェーズ 1 の中心になる、大きな変更です。

■ 最初に読むもの
1. CLAUDE.md(全部)
2. docs/PHASE1_PLAN.md の P0(全部)、P1.3、P1.4、P2(全部)、P3(全部)、P4.4、P5.2、P6
3. docs/DESIGN.md の D1、D2、D3、D9、§4-Q5、§4-Q7、§5.1、§5.2、§5.3、§5.5、§5.6、§8 のフェーズ 1
4. core の現行の実装(src/runner.cpp、src/tests/timed_*、src/diagnostics.cpp、src/summary.cpp、
   src/result_json.cpp、tests/)と、android/、ios/ のブリッジ、パーサ、画面

■ 前提
main に、docs/PHASE1_PLAN.md の PR-B(benchmark_version 0.2.2、schema 7)と
PR-C(core/src/window_recorder.h、core/src/tests/timed_unit_test.h)が入っていること。
入っていなければ、作業を始めずに報告してください。

■ やること
docs/PHASE1_PLAN.md P4.4 の「作業」の 1〜9 です。要点は次のとおりです。
- スコア対象の 4 テストを、固定時間方式にする(窓 200 ms × 5、その前にウォームアップ 200 ms。6 つの窓は止めずに続ける)
- 単位を、篩は上限 1,000,000、行列積は n=576 の 81 等分にする。マルチの 16 単位の調整を廃止する
- ピーク段階の手順を、docs/DESIGN.md §5.5 のとおりにする(開始待ち 10 秒、確かめ直しは 2 回まで、合計 30 秒)
- 確認ごとの値、採用しなかった回、回ごとのタッチの数を記録する
- JSON を schema 8 にし、要約の書式、analyze_summary.py、Kotlin と Swift のパーサ、画面を合わせる
- 診断モードを新しい方式に合わせる
- benchmark_version を 0.3.0 にする。基準値は、開発環境で測った暫定値を入れる
- README、docs/DESIGN.md、CLAUDE.md を直す

■ 進め方
- コミットは、P4.4 の作業の番号を目安に分けてください。作業 1〜5 が済んだ時点で、手元の ctest が通る状態にしてください。
- 1 本の PR に収めるのが難しい場合は、P4.4 の「大きすぎる場合の分け方」に従ってよいです。分けた場合は、報告してください。

■ やらないこと
- docs/PHASE1_PLAN.md P1.4 の項目(分野別スコア、status と null、警告と comparable、注意書き、
  カーネルのコード生成、クロックの求め方の入れ替え、診断モードの整理など)
- 判定の値(95%、3%、6%、測り直し 2 回)と、§5.5 の初期値、窓の長さと数の変更
- コンパイルフラグの変更。カーネルの内側のループの書き方の変更
- スレッドをコアに固定すること。片方の OS でしか取れない情報を判定に使うこと
- 基準値を、実測以外で決めること(基準機の診断の値や換算を使わない)
- 計測区間(6 つの窓の最初から最後まで)の中での、メモリの確保、ログの出力、ファイル I/O、JNI や Objective-C の呼び出し

■ 確かめ方
- cmake でビルドして ctest を通してください。
- 時間に依存するテストは、docs/PHASE1_PLAN.md P2.4 の書き方にしてください。Runner のテストは、
  窓ごとの値を自分で返す偽のテストで書きます。
- ./build/mobilebench_cli --quiet を 3 回実行し、4 テストとも有効であること、要約が
  analyze_summary.py --strict で読めること、パートが 8,000 バイト以内であることを確かめてください。
- push の後、Core tests、Android、iOS の 3 つのワークフローが成功することを確かめ、失敗したら直してください。
- JNI の進捗のシグネチャは実行時にしか検査されません。Kotlin の宣言と jni_bridge.cpp の文字列を、
  PR の説明に並べて書いてください。
- CLI で測った所要時間(診断モードのあり・なし)を、PR の説明に書いてください。画面の所要時間の文言も、それに合わせてください。

■ push と PR
- この依頼が、push と PR 作成の指示です。作業ブランチに push し、main への PR を作成してください。
- マージはしないでください。
- PR の説明は docs/PHASE1_PLAN.md P2.8 の 6 項目で書いてください。
  「マージ後に実機で確かめること」には、P5.2 の手順を写してください。

■ 迷ったとき
docs/PHASE1_PLAN.md P2.7 に従ってください。JSON のキー、要約の書式、手順の値、判定に関わる点で、
計画と docs/DESIGN.md から決まらないものは、進めずに私に尋ねてください。
計画と違う実装にした点は、PR の説明と docs/PHASE1_PLAN.md P7 に書いてください。
```

### P4.5 PR-R: 実機の結果の記録

**目的**: 実機で取った要約を保存し、数値を取り出して、`docs/DESIGN.md` に反映する。PR #4 と同じ種類の作業である。P5.1 の後と、P5.2 の後に行う。

**版**: 変えない。文書と記録だけの変更にする。

**作業**

1. 受け取った要約を、書き換えずに `docs/measurements/<受け取った日>-<端末>/` に保存する(`CLAUDE.md` の「ビルドとテスト」)。全パートの終端の行を確かめる。
2. `analyze_summary.py` で数値を取り出す。スクリプトが読めない行があれば、スクリプトを直す。
3. `docs/measurements/README.md` の一覧に足す。
4. `docs/DESIGN.md` に、新しい節(§1.9 以降)として事実を記録する。§6 の「結果」の表を更新する。
5. P0.2 の K1〜K5 のうち、判断が要るものについて、選択肢と推奨を PR の説明にまとめる。**判断そのものは、依頼者が行う。** 決定事項(D1〜D9)と、§4・§5 の方針は、依頼者の指示なしに書き換えない。

**プロンプト**(`<…>` の部分を埋めて渡す)

```text
MobileBench(リポジトリ Zuikakulove/mobile-bench)の作業です。
実機で取った結果を記録してください(docs/PHASE1_PLAN.md の PR-R)。

■ 最初に読むもの
1. CLAUDE.md(全部。特に「ビルドとテスト」の、実機の結果の受け取り方)
2. docs/PHASE1_PLAN.md の P0.2、P4.5、P5(全部)
3. docs/DESIGN.md の §1.7 と §1.8(書き方の手本)、§6、§8 のフェーズ 1
4. docs/measurements/README.md

■ 今回の結果
- どの確認か: <docs/PHASE1_PLAN.md P5.1(PR-B の後)/ P5.2 の段階 1 / P5.2 の段階 2>
- 端末と条件: <端末、性能モード、電池、充電の有無、機内モード、室温など。触れたかどうか>
- この後に、要約のパートを順に貼ります。全部を貼り終えたら「以上です」と書きます。それまでは保存だけしてください。

■ やること
docs/PHASE1_PLAN.md P4.5 の「作業」の 1〜5 です。
- 要約は、書き換えずに保存してください。パートの欠けや切れがあれば、作業を進める前に知らせてください。
- 数値は analyze_summary.py で取り出してください。手で写さないでください。
- 事実と推測を分けて書いてください(docs/DESIGN.md §0 の印を使う)。
- P0.2 の K1〜K5 に当たるものがあれば、選択肢と推奨を PR の説明にまとめてください。判断は私がします。

■ やらないこと
- コード、判定の値、手順、単位の変更
- 決定事項(D1〜D9)と、採用済みの方針の書き換え
- benchmark_version と schema_version の変更

■ push と PR
- この依頼が、push と PR 作成の指示です。作業ブランチに push し、main への PR を作成してください。
- マージはしないでください。
```

---

## P5. 実機の確認の手順(依頼者向け)

### P5.0 共通

- **入れるビルド**: PR をマージした後に、`main` に対して動いた Android のワークフローの成果物(`MobileBench-debug-apk`)。手順は `README.md` の「APK のダウンロードからインストールまで」にある。アプリの上部に出る版、ビルド番号、コミットが、マージ後のものであることを見る。
- **端末**: 基準機(Nothing Phone (1))と HONOR 端末(性能モードは「標準」)。
- **条件**(D2 の手順 1): 充電しない。電池 50% 以上。省電力モードはオフ。機内モード。開始のタッチの後は、端末を置いて触れない(触れることを確かめる回を除く)。
- **渡し方**: 「要約をコピー」のテキストを、パートごとに貼る。どの確認の何回目かを、一言添える。
- iPhone では行わない(保留中)。

### P5.1 PR-B の後(0.2.2)

2 台とも行う。0.2.2 は、計測の手順が 0.2.1 と同じである。HONOR 端末では、回復待ちのために 1 回の実行に 5 分以上かかる(§1.8 (3))。

| 順 | 行うこと | 見るところ | §6 の項目 |
|---|---|---|---|
| 1 | 診断モードをオンにして、触れずに 1 回実行する。全パートを貼る | `input touches 0` と出ること。`probe` の各行に `current` と `inload` が出ていること。集計の行の 4 つの幅(目標は 2% 以内) | V22(候補の比較の 1 回目)、V26 |
| 2 | 3 分以上あけて、同じことをもう 1 回 | 同上(1 と合わせて、端末ごとに 24 個になる) | V22 |
| 3 | 診断モードをオフにして実行し、計測中に、画面のボタンのない所に 3 回触れる。触れたのが何本目のテストのころかを控える | `input touches 3` と時刻。触れたころのテストの見出しに `touches` が出ること | V26 |
| 4 | 診断モードをオフにして実行し、計測中にホーム画面へ戻る。数秒してアプリに戻る | 画面に、前面を離れたための無効と出ること。要約をコピーできれば貼る(`INTERRUPTED(left_foreground)`) | V25 |
| 5 | (できれば)計測中に端末を回転させる。自動回転がオフなら省く | 無効にならないこと | V25 |

- HONOR 端末の 3 は、触れた後に「中止」を押してよい。その場合、「中止」を押した分が 1 回加わる。
- V25 と V26 は、PR-B が §6 に足す項目である。
- 結果は PR-R で記録する。意図どおりでなければ、PR-D のマージの前に直す(K5)。
- この確認を省いて、3〜5 を PR-D のビルドで(P5.2 の段階 1 の前に)行うこともできる。その場合、問題が見つかるのが遅れる。V22 は、P5.2 の段階 1 でも取れる。

### P5.2 PR-D の後(0.3.0)

2 台とも行う。段階 1 の結果を確かめてから、段階 2 に進む。**段階 1 と段階 2 は、同じビルドで行う**(§1.8 (4)。途中で APK を入れ替えない。OS も更新しない)。

**段階 1: 動作と中身の確認**(診断モードをオンにして 2 回。間は 3 分以上。触れない)

| §6 の項目 | 見るところ | 見込み |
|---|---|---|
| — | 全パートに終端の行があり、貼り付けで欠けないこと | 2 パート |
| V4 | マルチのテストの窓の行。窓ごとの単位数と `ns_per_unit`。ばらつきが 6% 以内か。窓を追って単位数が下がっていないか | — |
| V15 | シングルのテストの `W` の行と、`#1`〜`#5` の行の `cpu` | 基準機では、`#1`〜`#5` が cpu7 だけになる(§1.7 (5)) |
| V5 (d) | `clock` の行の `touch` と `baseline`。テストの見出しの `wait`、`chk`、`(NOT recovered)`。`rej` の行。`input touches 0` | HONOR 端末で、`baseline` が操作のない状態の値になり(0.2.1 では 2.40 GHz)、「回復せず」が付かない(D2)【推測】 |
| V21(測り直し) | クラスタ別の、cpu0〜3 の `matmul576p` と `sieve1m` の時間と `max` | 基準機で 10 ms 以下 |
| V22 | `probe` の集計の行 | 幅が 2% 以内の候補があるか |
| V7 に当たる確認 | `precondition` の時間と `settled` | 上限(10 秒)に達しない |
| — | 計測にかかった時間(おおよそでよい) | 約 1 分。診断モードではさらに 1〜2 分【推測】 |

**段階 2: 受け入れ基準**(V14。診断モードをオフにして 10 回。間は 3 分以上。触れない)

- 毎回、要約を貼る(1 回 1 パートの見込み)。
- 見るところ: テストと段階ごとの、スコアの変動係数(目標は、ピーク 2% 以下、持続 3% 以下)。「回復せず」の数。採用しなかった回。タッチが 0 であること。
- 10 回の途中で条件が変わったら(充電した、触れた、通知で中断された、など)、その回を控えておく。

### P5.3 フェーズ 1 の完了の判定

- 条件(§8): 基準機と 8 Elite 搭載機の両方で、4 テストとも、ピークの変動係数が 2% 以下であること。
- 変動係数は、段階 2 の 10 回のスコアの、標準偏差(n − 1)÷ 平均とする。`analyze_summary.py` が出す。
- 満たさないテストがあれば K2、HONOR 端末に「回復せず」が続けば K1 に従って、依頼者が判断する。
- 満たしたら、PR-R で §6 の結果と §8 の状態を更新する。基準値は暫定のままである(本測定はフェーズ 3)。

---

## P6. リスクと対処

| リスク | 対処 |
|---|---|
| ビルドごとにカーネルの位置が動き、遅いコアの篩が約 3 割変わりうる(§1.8 (4))。フェーズ 1 の各 PR でも起こりうる | PR-A の C8 で、ビルドごとの位置を残す。診断モードの `sieve` で、PR ごとに追う。V14 は同じビルドで測る。ビルドの違うスコアを比べない。原因の調査と対策は、フェーズ 2 で行う |
| 時間に依存するテストが、CI で揺れる | P2.4 の書き方にする |
| JNI や、JSON とパーサの食い違いが、実機でしか分からない | PR-A の検査(J7) |
| iOS の、前面を離れたときの扱いとタッチの検出を、確かめられない | J12。「保留: iOS 実機」の確認項目に足す |
| PR-D が大きい | PR-C で部品とテストを先に入れる。コミットを分ける。P4.4 の分け方を使う |
| HONOR 端末が、新しい手順でも「回復せず」になる | K1 |
| 受け入れ基準を満たさない | K2 |
| 片の最大時間が 10 ms を超える | K4 |
| 計測の後の完了処理(行列積の残りの片。基準機の遅いコアで最大約 0.57 秒の見込み)が、次のテストに響く | ピークでは、その後に 2 秒の休みと確認がある。持続は、もともと負荷を続ける段階である。影響は小さいと見込む【推測】。V14 の持続の変動係数で確かめる |
| 開始待ちの 10 秒の間に、利用者が画面に触れる | 画面に「端末を置いて、画面に触れないでください」と出す(§5.5)。触れたら記録に残る(P3.11) |
| Xcode のパッチ版の入れ替えで、iOS の CI が失敗する | J8。想定の値を直す PR を出す |
| 実機で使った APK の成果物が、期限で消える(build 12 は 2026-12-11) | J9 |

---

## P7. 実装時の変更の記録

計画と違う実装にした点を、PR ごとに 1 行ずつ足す。

| PR | 節 | 計画 | 実装 | 理由 |
|---|---|---|---|---|
| (まだない) | | | | |

---

## 付録: 確認した事実と出典(2026-11-16)

| 事実 | 確かめ方 |
|---|---|
| PR #4 の先頭は `5385406`。Android の実行 18・19、iOS の実行 17・18 は成功している(iOS は再実行の 2 回目で成功) | GitHub Actions の実行の一覧【実行】 |
| iOS のワークフロー(実行 `43782345849`)は、ラベル `macos-latest` で、イメージ `macos-26-arm64`(版 `20261102.0473.1`)、Xcode 26.6(Build version 17F113)、AppleClang 21.0.0.21000101 で動いた | そのジョブのログ【実行】 |
| 同じ実行で、`ctest` は 182.60 秒(`core_unit_tests` 2.65、`cli_smoke` 55.11、`cli_diagnostics` 124.84) | 同上【実行】 |
| `macos-latest` と `macos-26` は、どちらも「macOS 26 Arm64」のイメージのラベルである | <https://github.com/actions/runner-images>(ページの内容を取り出すツールで確認した)【資料】 |
| イメージ `macos-26-arm64` の `20261102.0473` には、Xcode 26.6(既定。17F113。`/Applications/Xcode_26.6.app`)、26.5、26.4.1、26.3、26.2、26.1.1、26.0.1 がある。Homebrew の LLVM は 20.1.8 | <https://github.com/actions/runner-images/blob/macos-26-arm64/20261102.0473/images/macos/macos-26-arm64-Readme.md>(同上)【資料】 |
| build 12 は、Android の実行 `42952127373`(2026-11-11、コミット `8145938`)。成果物の期限は 2026-12-11T03:23:08Z | GitHub の API の `expires_at`【実行】 |
| build 15 は、Android の実行 `43670028049`(2026-11-15、コミット `7dcf4a1`)。成果物の期限は 2026-12-15T02:57:25Z | 同上【実行】 |
| PR #4 の版の core は、手元(x86_64、論理 4 コア、GCC 13.3.0)でビルドでき、`ctest` は 3 つとも成功した(80.06 秒。`core_unit_tests` 1.98、`cli_smoke` 12.50、`cli_diagnostics` 65.58) | 手元で実行【実行】 |
| P3.21 の試作の数値 | 手元で実行(clang 18.1.3、GCC 13.3.0)【実行】 |

「資料」の 2 行は、作業の時点で変わっている可能性がある。PR-A のセッションが、もう一度確かめる。
