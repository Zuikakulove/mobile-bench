package com.mobilebench.app.bench

import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * core の CLI が出した JSON を、アプリのパーサ ([BenchmarkResult.parse]) に読ませる (docs/PHASE1_PLAN.md J7 の 2)。
 * JSON の形とパーサの食い違いを、実機に入れる前に CI で見つけるためのテスト。
 *
 * JSON のパスは環境変数 MB_CLI_JSON で渡す (.github/workflows/android.yml が、ホスト向けにビルドした
 * mobilebench_cli で `--only cpu.single.sieve --diagnostics --quiet` の JSON を作る)。未設定なら飛ばす。
 */
class BenchmarkResultParseTest {
    @Test
    fun parsesCliJsonWithDiagnostics() {
        val path = System.getenv("MB_CLI_JSON")
        assumeTrue("MB_CLI_JSON is not set; skipped", !path.isNullOrEmpty())
        val json = File(path!!).readText(Charsets.UTF_8)

        val result = BenchmarkResult.parse(json)  // 例外が出ないこと

        assertTrue("tests is empty", result.tests.isNotEmpty())
        assertTrue("summaryParts is empty", result.summaryParts.isNotEmpty())
        assertTrue("sweep (diagnostics.matmul_sweep) is empty", result.sweep.isNotEmpty())
    }
}
