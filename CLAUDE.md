# MobileBench — Claude Code 用ルール

iOS と Android の端末性能を、同じ数値の基準で比べるベンチマークアプリ。
**最優先: OS や GPU API が違っても、スコアが同じ意味を持つこと。** 迷ったらこれで判断する。

根拠、検討した選択肢、未決事項の推奨案、ロードマップは `docs/DESIGN.md` にある。
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

1. スコアの基準は Snapdragon 765G。各テスト 1000 点とし、「基準の時間 ÷ 計測時間 × 1000」で算出する。総合スコアは幾何平均。
2. 基準値は 765G で測った結果を、定数としてコードに埋め込む(`reference_times.h`)。
3. 計測は固定回数ではなく固定時間方式(一定時間内にこなせた処理単位数で数える)。
4. GPU テストは画面に出さないオフスクリーン描画で行い、リフレッシュレート上限を回避する。
5. GPU の機能要件は Vulkan 1.1 の範囲に収める。
6. Android 専用の情報(CPU 周波数、クラスタ構成など)はスコア計算に使わず、参考値として JSON に記録する。
7. テスト内容を変更したら `benchmark_version` を上げ、基準値も測り直す。

各決定の具体的な定義は `docs/DESIGN.md` §3 を見る。

## 決定に実装が追いついていない点(解消したらこの節から消す)

- 計測はまだ固定仕事量方式(`core/src/registry.cpp` の単位数、`UnitBasedTest::run_once`)。
- `reference_times.h` は開発用 x86_64 環境の暫定値。765G の実測値に替えるまで、スコアを 765G 基準として扱わない。
- GPU テストは未実装。
- iOS は実機向けのビルド・署名が未整備(シミュレータ向けビルドのみ)。

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

- `benchmark_version`(`core/include/mobilebench/version.h`)は、計測区間の処理、処理単位、計測手順、基準値、コンパイルフラグのどれかを変えたら上げる。同じバージョン同士でしかスコアを比べない。
- 上げるときは、`android/app/build.gradle.kts` の `versionName`、`ios/project.yml` の `MARKETING_VERSION`、`version.h` の履歴コメントを同時に更新する。

## テストを追加する手順

1. `core/src/tests/` に `mb::BenchmarkTest` の実装を足す。準備は `setup()` に置き、計測対象だけを計測区間に入れる。
2. `reference_times.h` に基準値を、`core/src/registry.cpp` に登録を足す。`TestInfo.id` は一度決めたら変えない。
3. `core/CMakeLists.txt` の `add_library` にソースを足す(iOS は `core/src` 以下を自動で取り込む)。
4. `core/tests/` にユニットテストを足し、`benchmark_version` を上げる。

## 禁止事項

- 依頼者の指示がない push、PR の作成、`main` の変更。
- 未決事項(`docs/DESIGN.md` §4)の推奨案を、承認の前に決定として実装すること。
- 基準値を実測以外(推定、換算、他機種の値)で決めること。
- 署名用の秘密情報(証明書、プロビジョニングプロファイル、API キー)、`.xcodeproj`、ビルド生成物のコミット。
- `android/app/debug.keystore` の差し替えと、配布用の署名への利用(差し替えると上書き更新できなくなる)。
- `.github/workflows/android.yml` のファイル名の変更(ビルド番号が 1 に戻り、上書きインストールできなくなる)。
- 外部仕様(Vulkan、Metal、OS の挙動など)を、未確認のまま断定して書くこと。未確認なら「推測」と明記する。
