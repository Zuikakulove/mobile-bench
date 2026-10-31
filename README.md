# mobile-bench

Android と iOS で**同じ基準のスコア**を出すスマホ向けベンチマークアプリ。
計測ロジックはすべて共通の C++17 コア (`core/`) にあり、各 OS のアプリは
「core を呼んで、返ってきた JSON を表示するだけ」の薄い UI です。

ビルドはすべて GitHub Actions で行います（PC 不要）。

- [ディレクトリ構成](#ディレクトリ構成)
- [計測ルール](#計測ルール)
- [Android と iOS で同じ基準にするための仕組み](#android-と-ios-で同じ基準にするための仕組み)
- [計測の内訳と診断モード（切り分け用）](#計測の内訳と診断モード切り分け用)
- [GitHub Actions](#github-actions)
- [APK のダウンロードからインストールまで（Android）](#apk-のダウンロードからインストールまでandroid)
- [iOS について](#ios-について)
- [テストの追加方法（Phase 2 以降）](#テストの追加方法phase-2-以降)
- [基準時間の差し替え方](#基準時間の差し替え方)

---

## ディレクトリ構成

```
core/                               計測ロジック (C++17)。Android / iOS / Linux で同一ソース
  include/mobilebench/
    runner.h                        公開 API (Runner, RunConfig, Progress)
    benchmark_test.h                テストのインターフェース (setup / run_once / teardown)
    result.h                        結果の構造体と to_json()
    reference_times.h               ★ 基準時間の一元管理 (暫定値)
    build_profile.h                 ★ コンパイルフラグプロファイル (-O2 等) の検査
    version.h                       ★ ベンチマークのバージョン
  src/
    runner.cpp                      ウォームアップ 1 回 → 本計測 5 回 → 中央値 → スコア
    stats.cpp                       中央値・スコア・幾何平均
    result_json.cpp, json_writer.h  JSON 出力 (ロケール非依存)
    registry.cpp                    既定スイートの登録 (テストを足す場所)
    worker_pool.*                   マルチスレッド用スレッドプール
    platform.*                      OS 依存処理の集約場所
    do_not_optimize.h               最適化による処理の削除を防ぐ
    tests/                          CPU テスト (篩・行列積)
  cmake/MobileBenchFlags.cmake      ★ コンパイルフラグ (CMake 側の唯一の定義)
  tests/                            ユニットテスト (外部依存なし / CTest)
  tools/cli_main.cpp                Linux / macOS 用 CLI (JSON を出力)

android/                            Kotlin + Jetpack Compose
  app/src/main/cpp/                 JNI ブリッジ + core を add_subdirectory する CMake
  app/src/main/java/.../bench/      NativeBench (JNI ラッパー), BenchmarkResult (JSON パース)
  app/src/main/java/.../            MainActivity, BenchmarkViewModel, ui/
  app/debug.keystore                固定デバッグ鍵 (更新時にアンインストール不要にするため)

ios/                                SwiftUI
  project.yml                       ★ XcodeGen 定義 (xcodeproj はコミットしない)
  MobileBench/Bridge/               Objective-C++ ブリッジ (MBBenchmarkBridge.mm)
  MobileBench/Model/, UI/, App/     ViewModel, JSON デコード, 画面

.github/workflows/
  core-test.yml                     Linux (GCC / clang) で core のユニットテスト
  android.yml                       デバッグ APK をビルドして Artifacts にアップロード
  ios.yml                           XcodeGen → 署名なしでシミュレータ向けビルド
```

## 計測ルール

すべて `core` に実装されており、OS 側では変更できません。

| 項目 | 内容 |
|---|---|
| 処理量 | 各テストは固定の処理量を実行（端末のコア数などに依存しない） |
| 手順 | ウォームアップ 1 回 → 本計測 5 回 → **中央値**を採用 |
| テストスコア | `基準時間 ÷ 計測時間(中央値) × 1000`（基準時間は `core/include/mobilebench/reference_times.h`） |
| 総合スコア | 各テストスコアの**幾何平均** |
| 結果 | JSON（テスト名、生の計測時間、中央値、スコア、ベンチマークのバージョン、ビルド情報、機種情報） |
| 時計 | `std::chrono::steady_clock`（単調増加） |

### Phase 1 のテスト

| ID | 内容 | 1 回の計測の処理量 |
|---|---|---|
| `cpu.single.sieve` | 整数: エラトステネスの篩（32KiB 区間篩、4,000万未満の素数を数える） | 4 単位 |
| `cpu.single.matmul` | 浮動小数点: 576×576 倍精度行列積（64 ブロック化） | 4 単位 |
| `cpu.multi.sieve` | 上と同じカーネルを論理コア数分のスレッドで実行 | 64 単位 |
| `cpu.multi.matmul` | 同上 | 64 単位 |

**マルチスレッドの設計について**：
「スレッドごとに同じ量」を実行すると、コアの多い端末ほど総処理量が増えてしまい、
「固定の処理量」という前提が崩れてスコアを比較できなくなります。そこで
**総処理量を 64 単位に固定**し、論理コア数分のワーカーが 1 単位ずつ取り合う方式にしています。
big.LITTLE 構成（速いコアと遅いコアが混在）でも、速いコアが多くの単位を処理するため偏りません。
スレッドは計測前に起動しておくので、スレッド生成のコストは計測時間に含まれません。

### JSON の例（抜粋）

```json
{
  "schema_version": 3,
  "benchmark_version": "0.1.1",
  "score_formula": "reference_ns / median_ns * 1000",
  "overall_score": 1070.750,
  "valid": true,
  "config": { "warmup_runs": 1, "measured_runs": 5, "logical_cores": 8 },
  "build": { "os": "android", "arch": "arm64", "compiler": "clang 18.0.x",
             "flags_profile": 1, "flags": "-O2 -ffp-contract=on", "optimized": true },
  "device": { "model": "Pixel 8", "os_version": "15", "soc_model": "..." },
  "tests": [
    { "id": "cpu.single.sieve", "name": "CPU Single-thread Integer (Sieve)", "threads": 1,
      "reference_ns": 150000000, "warmup_ns": 160200428,
      "runs_ns": [138258477, 160725437, 165166472, 141819029, 146931311],
      "median_ns": 146931311, "score": 1020.888, "valid": true,
      "checksum": "9734616", "expected_checksum": "9734616",
      "run_details": [ ... 下記「計測の内訳」参照 ... ] }
  ],
  "cpu_topology": { ... }
}
```

## Android と iOS で同じ基準にするための仕組み

1. **同一ソース**：`core/` を Android（NDK clang）と iOS（Apple clang）でそのままコンパイルします。
2. **コンパイルフラグの統一**：フラグは「プロファイル 1」（`-O2 -ffp-contract=on`、C++17）として定義しています。
   - Android / Linux：`core/cmake/MobileBenchFlags.cmake`（ビルドタイプが Debug でも必ず `-O2`）
   - iOS：`ios/project.yml`（`GCC_OPTIMIZATION_LEVEL=2`。Debug / Release 共通）
   - どちらも `MB_FLAGS_PROFILE=1` を定義します。未定義だと `build_profile.h` が `#error` でビルドを止めます。
   - 実際に最適化が有効だったか（`__OPTIMIZE__`）とコンパイラ名を JSON の `build` に記録します。
   - CI でも検査します：Android は `compile_commands.json` から core の全ソースが `-O2 -ffp-contract=on -DMB_FLAGS_PROFILE=1` でコンパイルされたことを確認し（`.github/scripts/check_android_flags.py`）、iOS は Xcode のビルド設定をログに出力します。
3. **最適化で処理が消されない工夫**：入力値を `launder()`（インライン asm）でコンパイラから「不明」に見せて定数畳み込みを防ぎ、結果は `do_not_optimize()` で使われたことにします。
4. **チェックサムによる自己検証**：各テストは計算結果のチェックサム（素数の個数、行列積の重み付き総和）を返し、毎回期待値と照合します。不一致なら `valid: false` になり、総合スコアは 0 になります。
   処理を省略されていないことと、両 OS で同じ計算をしたことが毎回の計測で保証されます。
5. **浮動小数点の再現性**：行列の要素は小さな整数値を `double` で表したものです。積和がすべて 2^53 未満の整数に収まるので、FMA の有無やベクトル化の違いに関係なく、結果は全プラットフォームでビット単位まで一致します（演算自体は通常の倍精度演算です）。
6. **スレッド条件**：論理コア数は Android ではオフラインのコアも含む `_SC_NPROCESSORS_CONF`、iOS では `hw.logicalcpu_max` で取得します。iOS は QoS によって P コアと E コアのどちらに割り当てられるかが変わるため、計測スレッドを `USER_INITIATED` に揃えています。
7. **JSON の小数はロケール非依存**：自前の整形で、常に `.` 区切りで出力します。

> 注：GCC と clang では最適化の度合いが違います（例：clang は `-O2` でも行列積をベクトル化するため 2 倍以上速い）。
> モバイルは両 OS とも clang 系なので条件は揃っていますが、Linux の GCC で測ったスコアはスマホのスコアと比べないでください。

## 計測の内訳と診断モード（切り分け用）

スコアの計算には使わない観測データを JSON に記録しています。
画面ではテストごとの「各回の内訳を表示」から見られます。

- **「要約をコピー」**：切り分けに必要な情報を1回1行にまとめた4〜5KBのテキストです（JSON の `summary_text`）。チャットに貼っても途中で切れません。**結果を共有するときは基本的にこちらを使ってください。**
- 「JSON をコピー」「共有」：JSON 全体です。診断モードでは数十KBになるので、チャットに貼ると切れることがあります。

### 常に記録するもの（`tests[].run_details`、ウォームアップの回も含む）

| フィールド | 内容 |
|---|---|
| `ns` | その回の計測時間（`runs_ns` と同じ値） |
| `unit_ns`, `unit_cpus` | シングルスレッド：作業単位（4つ）ごとの時間と、開始時に動いていた CPU 番号 |
| `workers[]` | スレッドごとの `units`（処理した単位数）、`busy_ns`（処理時間）、`cpus`（動いた CPU 番号） |
| `workers[].ghz_before` / `ghz_after` | 計測の直前・直後に各スレッドで推定した**実効クロック**（GHz） |
| `cpu_khz_before` / `cpu_khz_after` | 計測の直前・直後の各コアの周波数（OS の報告値、kHz） |
| `cpu_cap_khz` | 計測直前の各コアの**周波数上限**（`scaling_max_freq`）。発熱や省電力でこの上限が下げられると、性能が頭打ちになる |
| `tests[].conditions_before` / `conditions_after` | テスト開始時・終了時の端末の状態（下記） |
| `cpu_topology` | 各コアの最大周波数、`cpu_capacity`、L2/L3 キャッシュのサイズと共有範囲（iOS は `perf_levels`） |

- **実効クロックの推定**：1サイクルで終わる整数加算を、前の結果に依存させて（並列に実行できないように）連続実行し、その時間から求めます。OS の権限が不要なので、Android と iOS で同じ方法を使えます。計測時間には含めません。
  - 加算は**レジスタ同士**で行います。即値の加算だと、一部の CPU（Intel Golden Cove 以降など）はリネーム段で命令を消してしまい、実際の5倍以上のクロックに見えるためです（開発環境で確認済み）。
- **OS 報告の周波数**：Android / Linux は sysfs の `scaling_cur_freq` から読みます。SELinux で拒否される端末や iOS では空配列になります。
- CPU 番号は Android / Linux では `sched_getcpu()` で取得します。iOS では取得できないので -1 または空になります。
- **端末の状態**（`conditions_*`）は各 OS の公式 API で取得します。
  - Android：`PowerManager` の `thermal_status`（NONE が通常）・`thermal_headroom_10s`（1.0 で発熱による制限が始まる目安）・`power_save`、電池の `battery_temp_c`・`battery_level`・`charging`
  - iOS：`ProcessInfo` の `thermal_state`（nominal が通常）・`low_power`

### 実機データからわかったこと（HONOR Magic7 RSR / Snapdragon 8 Elite）

- シングルスレッドの結果が 110ms と 146ms の2通りに分かれたのは、**どの回もプライムコア（cpu6・7）で動いたうえで、プライムクラスタの周波数が 4320MHz か 3072MHz かの違い**でした。3072MHz の回は、計測の最初から最後までその値に固定されていました。
- 時間の比（1.33）は、実効クロック推定値の比（4.09/3.07 = 1.33）とぴったり一致しました。OS 報告値の比（4.32/3.07 = 1.41）とは一致しません。実際の性能を表すのは推定値のほうです（OS が 4320MHz と報告している間も、ハードウェア側の制限で実効は 4.09GHz 程度だったと考えられます）。
- 同じ状態の中では、計測ごとの差は1%未満でした。つまりばらつきは、計測方法ではなく**端末の状態（周波数上限）**から来ています。
- **計測区間（時間を測る部分）の処理は 0.1.0 と同じ**です。ただし計測の前後にクロック推定が加わったので、規則に従ってバージョンを 0.1.1 に上げました。

### 診断モード（スイッチでオン、スコアには含めない）

スコア対象のテストの後に、次の計測を追加で行います（数秒〜十数秒）。

- **行列サイズの掃引**（`diagnostics.matmul_sweep`）：n = 64〜768 について、シングルとマルチの GFLOPS と作業領域の大きさ（WS）を測ります。
  WS が L2 キャッシュ（`cpu_topology.caches`）を超えたところで性能が落ちるかどうかで、キャッシュやメモリ帯域が原因かを判別できます。
- **周波数の定期記録**（`diagnostics.freq_samples`）：計測中、100ms ごとに各コアの周波数を記録します（Android / Linux のみ）。
  `run_details[].t_start_ms` と同じ時間軸なので、どの計測の最中に周波数が下がったかを対応づけられます。

CLI でも `mobilebench_cli --diagnostics` で同じ計測ができます。

## GitHub Actions

| ワークフロー | 実行条件 | 内容 |
|---|---|---|
| `core-test.yml` | `core/` の変更 | Ubuntu 上で GCC と clang の両方でビルドし、ユニットテストと全テストの実行（チェックサム確認）を行う |
| `android.yml` | `android/` または `core/` の変更、手動 | デバッグ APK をビルドし、Artifacts に `MobileBench-debug-apk` としてアップロード（30 日保持） |
| `ios.yml` | `ios/` または `core/` の変更、手動 | Apple clang で core のユニットテスト → XcodeGen → 署名なしでシミュレータ向けにビルド |

どれも、Actions タブでワークフローを選んで「Run workflow」を押せば手動でも実行できます。

### 開発環境についての注意

この構成は「Android SDK や Xcode がない環境で書き、検証は Actions で行う」前提です。
- core：Linux 上で CMake / CTest を使ってビルド・テストできます。
  ```bash
  cmake -S core -B build && cmake --build build --parallel && ctest --test-dir build --output-on-failure
  ./build/mobilebench_cli          # JSON を出力
  ```
- Android / iOS：ビルドできるかは Actions の結果で確認します。

## APK のダウンロードからインストールまで（Android）

スマホだけで完結する手順です。

### 1. APK をダウンロードする

1. スマホの**ブラウザ**（Chrome など）で GitHub にログインし、このリポジトリを開きます。
   - GitHub の公式アプリでは Artifacts をダウンロードできないことがあるため、ブラウザを使ってください。
   - Artifacts のダウンロードにはログインが必要です。
2. 上部の **Actions** タブ → 左の一覧（スマホでは「All workflows」のメニュー）から **Android** を選びます。
3. 一覧から、緑のチェック ✅ が付いている最新の実行を開きます。
   - 実行がない、または古い場合は「Run workflow」→「Run workflow」で手動実行し、数分待ちます。
4. ページ下部の **Artifacts** 欄にある **MobileBench-debug-apk** をタップします。zip ファイルがダウンロードされます。
   - Artifacts 欄が見つからないときは、ブラウザのメニューで「PC 版サイト」をオンにすると表示されます。

### 2. zip を展開する

1. 「Files by Google」などのファイルアプリで「ダウンロード」フォルダを開きます。
2. `MobileBench-debug-apk.zip` をタップして **展開（解凍）** します。
3. 中に `MobileBench-debug-b<ビルド番号>-<コミットID先頭7文字>.apk` が入っています。

### 3. インストールする

1. `.apk` ファイルをタップします。
2. 「このソースからのアプリはインストールできません」と表示されたら **設定** → **この提供元のアプリを許可** をオンにして戻ります。
   （許可が必要なのは、APK を開いたアプリ＝ファイルアプリやブラウザです。後から「設定 → アプリ → 特別なアプリアクセス → 不明なアプリのインストール」でオフに戻せます。）
3. **インストール** をタップします。
   - Google Play プロテクトの警告が出た場合は「詳細」→「このままインストール」を選びます（自分でビルドした未登録のアプリなので出る警告です）。
4. ホーム画面またはアプリ一覧の **MobileBench** を開き、**計測開始** を押します。

### 更新するとき・ビルド番号

`versionCode`（ビルド番号）は GitHub Actions の **Android ワークフローの実行番号**（`GITHUB_RUN_NUMBER`）から自動で決まり、ビルドのたびに増えます。
アプリ上部に `アプリ 0.1.1 (build 12 · 1b8ba08)` のように表示され、結果 JSON の `device.app_build` にも記録されます。
ローカルでビルドした場合は `build 1 · local` になります。

> 注意：実行番号はワークフローごとに数えられます。`android.yml` のファイル名を変えると番号が 1 から数え直しになり、インストール済みのものより古い番号になってしまうため、上書きインストールできなくなります。


同じ手順で新しい APK をダウンロードし、そのままインストールすれば上書き更新されます。
APK はリポジトリ内の固定デバッグ鍵（`android/app/debug.keystore`）で署名しているので、
アンインストールは不要です（この鍵はデバッグ専用です。Play ストア配布用には使わないでください）。

### 計測のコツ

- 充電中、本体が熱い状態、省電力モードではスコアが下がることがあります。
- 対応 ABI は `arm64-v8a`（実機）と `x86_64`（エミュレータ）です。32bit のみの端末には対応していません。

## iOS について

- 現在は Actions の macOS ランナーで「署名なしでシミュレータ向けビルドが通ること」だけを確認しています。
- 実機で動かすには Apple Developer Program への登録（または無料の個人チームでの署名）と、署名用の証明書・プロファイルを Actions のシークレットに登録する作業が必要です。登録後に `ios.yml` へ実機用（`-sdk iphoneos`）のジョブを追加する予定です。
- Xcode プロジェクトは `ios/project.yml` から XcodeGen で生成するので、`.xcodeproj` はリポジトリに含めていません。

## テストの追加方法（Phase 2 以降）

1. `core/src/tests/` に `mb::BenchmarkTest` を実装したクラスを追加します。
   - `setup()`：計測対象外の準備（バッファ確保、ファイル作成、スレッド起動）
   - `run_once()`：計測対象。固定の処理量を 1 回実行し、チェックサムを返す
   - `expected_checksum()`：正しいチェックサム（`setup()` の後に呼ばれる）
   - `teardown()`：後始末
2. `core/include/mobilebench/reference_times.h` に基準時間を追加します。
3. `core/src/registry.cpp` に 1 行追加します。
4. `core/CMakeLists.txt` の `add_library` にソースを追加します（iOS は `core/src` 以下を自動で取り込みます）。
5. `version.h` のバージョンを上げます。

UI は JSON の `tests` 配列を表示しているだけなので、変更は不要です。

予定している拡張と、その置き場所：

| 予定 | 方針 |
|---|---|
| メモリ帯域 | 大きなバッファの `memcpy`。`setup()` でバッファを確保してページを割り当て済みにしておく |
| ストレージ順次読み書き | `TestContext::scratch_dir`（Android は `cacheDir`、iOS は `NSTemporaryDirectory()` を渡し済み）にファイルを作る。OS キャッシュの回避（Android は `O_DIRECT`、iOS は `fcntl(F_NOCACHE)`）は `core/src/platform.*` に関数として追加する |
| GPU | API の差が大きい（Vulkan / Metal）ため、最後に検討 |
| サーマルスロットリング | 長時間の連続実行で時系列を記録する。Runner の別モードとして、JSON にテストとは別の配列を追加する（`schema_version` を上げる）。中断には既存の `Runner::cancel()` を使う |

## 基準時間の差し替え方

現在の `reference_times.h` の値は、開発用 Linux コンテナ（x86_64、論理 4 コア、clang 18）で
測った**暫定値**です（その環境でスコアが約 1000 になります）。基準とする実機を決めたら：

1. 基準機でアプリを実行し、「結果 JSON を表示」から各テストの `median_ns` を控えます。
2. `core/include/mobilebench/reference_times.h` の値を差し替えます。
3. `core/include/mobilebench/version.h` の `kBenchmarkVersion` を上げます（スコアは同じバージョン同士でしか比較できません）。
