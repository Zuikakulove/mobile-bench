# MobileBench — Claude Code 用ルール

iOS と Android の端末性能を、同じ数値の基準で比べるベンチマークアプリ。
**最優先: OS や GPU API が違っても、スコアが同じ意味を持つこと。** 迷ったらこれで判断する。

根拠、検討した選択肢、実機で確かめる項目、ロードマップは `docs/DESIGN.md` にある。
`README.md` と食い違う場合は、本ファイルと `docs/DESIGN.md` を優先する。

## 主要ディレクトリ

- `core/` 計測ロジック(C++17)。Android / iOS / Linux で同一ソース
  - `include/mobilebench/` 公開 API。`runner.h`(`mb::Runner`、`RunPolicy`)、`benchmark_test.h`(`mb::BenchmarkTest`)、`result.h`、`reference_times.h`(基準値)、`version.h`、`build_profile.h`
  - `src/runner.cpp` 計測手順 / `src/registry.cpp` 既定スイート / `src/tests/` テスト本体 / `src/platform.*` OS 依存処理
  - `tests/` ユニットテスト(`mini_test.h`)/ `tools/cli_main.cpp` CLI / `cmake/MobileBenchFlags.cmake` コンパイルフラグ
- `android/` Kotlin + Jetpack Compose。`app/src/main/cpp/jni_bridge.cpp` が core を呼ぶ(`libmobilebench_jni.so`)
- `ios/` SwiftUI + Objective-C++ ブリッジ。`project.yml`(XcodeGen)から Xcode プロジェクトを生成する
- `.github/workflows/` `core-test.yml` / `android.yml`(デバッグ APK)/ `ios.yml`(シミュレータ向けビルド)

## ビルドとテスト

```bash
cmake -S core -B build && cmake --build build --parallel
ctest --test-dir build --output-on-failure        # 約 1 分
./build/mobilebench_tests <テスト名の一部>          # ユニットテストを絞って実行
./build/mobilebench_cli --only cpu.single.sieve   # 1 テストだけ計測して JSON を出力
```

- core を変更したら、コミット前に上の `ctest` を通す。
- Android / iOS はこの環境ではビルドできない。push 後に GitHub Actions の結果で確認する。
- 実機の結果は、アプリの「要約をコピー」のテキストで受け取る(JSON 全体はチャットで途切れる)。
- x86_64、シミュレータ、エミュレータのスコアは動作確認用。実機のスコアと比べない。

## 決定事項(変更には依頼者の承認が必要)

番号は `docs/DESIGN.md` §3 と同じ。具体的な定義もそこにある。

- **D1 スコア**: 基準は Snapdragon 765G で、各テスト 1000 点。スコア = 基準機の 1 単位あたりの時間 ÷ 計測した 1 単位あたりの時間 × 1000。
  総合スコアは、分野ごとにテストを幾何平均し、分野を重みつきで幾何平均する。重みは CPU シングル 3:CPU マルチ 3:GPU 3:メモリ 1。必須の分野がすべて有効なときだけ出す。
- **D2 基準値**: 765G で測った結果を、定数としてコードに埋め込む(`reference_times.h`)。測るたびに 8 Elite 搭載機でも同じ手順で測り、結果を一緒に保存する。
- **D3 計測方式**: 固定回数ではなく固定時間方式(一定時間内にこなせた処理単位数で数える)。
- **D4 GPU の描画**: 画面に出さないオフスクリーン描画で行い、リフレッシュレート上限を回避する。
- **D5 GPU の機能**: Vulkan 1.1 の範囲に収める。
- **D6 OS 固有の情報**: Android 専用の情報(CPU 周波数、クラスタ構成など)はスコア計算に使わず、参考値として JSON に記録する。
- **D7 版**: テスト内容を変更したら `benchmark_version` を上げ、基準値も測り直す。
- **D8 比較の扱い**: iOS と Android の比較は、コンパイラ差を確認できるまで暫定扱いとする。GPU テストは、出力の照合が両 OS の実機で通るまで暫定扱いとする。
- **D9 メーカーの制御**: 特定メーカーの省電力制御(HONOR 端末のタッチ直後だけ高性能になる挙動など)に合わせて、テストの処理量を変えない。待機や判定の値も同じ扱いとする。

## 採用済みの方針(根拠と詳細は `docs/DESIGN.md` §4・§5)

- **GPU API**: Android は Vulkan、iOS は Metal。シェーダーは 1 本の GLSL から、SPIR-V と Metal 用をビルド時に生成する。MoltenVK は使わない。
- **CPU カーネル**: スカラーのループは自動ベクトル化を止める。SIMD を測る最内ループだけ NEON の組み込み関数で書く。指定はソース内に書き、生成コードの一致を CI で検査する。
- **テスト構成**: CPU 4 本に、`mem.single.copy`、`mem.multi.copy`、`gpu.fill`、`gpu.geom`、`gpu.compute` を足す。ストレージは総合に入れない。
- **処理単位**: 均質で、チェックサムを確かめられる大きさにする。最も遅いコアで窓の 1/20 以下、最も速いコアで 100 µs 以上。締切を過ぎたら実行中の単位を完了し、ワーカーごとの(完了数 ÷ 実時間)を合計する。
- **発熱**: ピークと持続は別スコアのままにし、1 つの数字に混ぜない。長時間モードは任意実行で、総合に入れない。
- **信頼性**: 現行の安定判定(ばらつき 6%、測り直し 2 回)は、実機データなしに変えない。計測中に前面を離れたら無効にする。
- **動かないテスト**: 状態(`unsupported` / `failed`)と理由を記録する。スコアは 0 ではなく値なし(`null`)にし、総合は出さない。
- **iOS**: `core/src` を Xcode で直接コンパイルする現行の構成を維持する。配布は TestFlight の方針だが、**Apple Developer Program の登録と iOS の実機検証は保留中**。
- **GPU の計測中表示**(§5.4): アニメーションはフレーム番号で進める。縮小画像を core から UI に渡して表示し、その間は計測の時計を止める。表示のあり・なしを切り替えられるようにする。基準値は「表示なし」で測り、「表示あり」は差が小さいと確認できるまで参考値とする。

## 決定に実装が追いついていない点(解消したらこの節から消す)

- 計測はまだ固定仕事量方式(`core/src/registry.cpp` の単位数、`UnitBasedTest::run_once`)。マルチの 16 単位は D9 に反する調整で、固定時間方式への移行で廃止する。
- `reference_times.h` は開発用 x86_64 環境の暫定値。765G の実測値に替えるまで、スコアを 765G 基準として扱わない。
- 分野別の総合スコア、メモリテスト、GPU テスト、計測中表示、「暫定」の表示は未実装。iOS はシミュレータ向けビルドのみ。

## 守るべき制約

- 計測手順、スコア計算、JSON 生成は core だけに置く。Kotlin / Swift は core の JSON を表示するだけにする。
- OS 依存の処理は `core/src/platform.*` と各ブリッジに閉じ込める。テスト本体に OS 別の分岐を書かない。
- スコア対象のテストでは、スレッドをコアに固定しない(`pin_current_thread` は診断モード専用)。マルチスレッドのスレッド数は論理コア数。
- 片方の OS でしか取れない情報を、待機・安定・有効性の判定に使わない(端末の状態の判定には、両 OS 共通の実効クロック推定を使う)。
- すべてのテストはチェックサムで結果を検証する。全プラットフォームでビット単位に一致する入力にする。
- 計測区間の中で、メモリ確保、ログ出力、ファイル I/O、JNI / Objective-C の呼び出しをしない。
- コンパイルフラグは `core/cmake/MobileBenchFlags.cmake` と `ios/project.yml` の 2 か所で同じ内容にする。他の場所で最適化フラグを足さない。
- 診断モードの計測はスコアに影響させない。
- JSON の形を変えたら `schema_version` を上げ、`BenchmarkResult.kt` と `BenchmarkResult.swift` を両方直す。小数は `JsonWriter::format_fixed` で出す。
- コメントと文書は日本語、識別子と JSON のキーは英語で書く。

## バージョン

- `benchmark_version`(`core/include/mobilebench/version.h`)は、計測区間の処理、処理単位、計測手順、基準値、コンパイルフラグ、分野の重みのどれかを変えたら上げる。同じバージョン同士でしかスコアを比べない。
- 上げるときは、`android/app/build.gradle.kts` の `versionName`、`ios/project.yml` の `MARKETING_VERSION`、`version.h` の履歴コメントを同時に更新する。

## テストを追加する手順

1. `core/src/tests/` に `mb::BenchmarkTest` の実装を足す。準備は `setup()` に置き、計測対象だけを計測区間に入れる。
2. `reference_times.h` に基準値を、`core/src/registry.cpp` に登録を足す。`TestInfo.id` は一度決めたら変えない。
3. `core/CMakeLists.txt` の `add_library` にソースを足す(iOS は `core/src` 以下を自動で取り込む)。
4. `core/tests/` にユニットテストを足し、`benchmark_version` を上げる。

## 禁止事項

- 依頼者の指示がない push、PR の作成、`main` の変更。
- `docs/DESIGN.md` で「初期値」「案」とした数値や方針を、実機データや承認なしに確定として扱うこと。
- 保留中の作業(Apple Developer Program の登録、iOS の署名、TestFlight での配布)を、指示なしに進めること。
- 基準値を実測以外(推定、換算、他機種の値)で決めること。
- 署名用の秘密情報(証明書、プロビジョニングプロファイル、API キー)、`.xcodeproj`、ビルド生成物のコミット。
- `android/app/debug.keystore` の差し替えと、配布用の署名への利用(差し替えると上書き更新できなくなる)。
- `.github/workflows/android.yml` のファイル名の変更(ビルド番号が 1 に戻り、上書きインストールできなくなる)。
- 外部仕様(Vulkan、Metal、OS の挙動など)を、未確認のまま断定して書くこと。未確認なら「推測」と明記する。
