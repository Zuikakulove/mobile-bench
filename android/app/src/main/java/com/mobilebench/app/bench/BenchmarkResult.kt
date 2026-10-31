package com.mobilebench.app.bench

import org.json.JSONArray
import org.json.JSONObject

/**
 * core が返す JSON (schema_version 3) の表示用モデル。
 * スコア計算などは一切行わず、JSON の値を読んで表示しやすい形にまとめるだけ。
 */
data class BenchmarkResult(
    val benchmarkVersion: String,
    val overallScore: Double,
    val valid: Boolean,
    val cancelled: Boolean,
    val logicalCores: Int,
    val build: Map<String, String>,
    val tests: List<TestEntry>,
    val topology: Topology,
    val sweep: List<SweepPoint>,
    val freqSampleCount: Int,
    val summaryText: String,
) {
    data class TestEntry(
        val id: String,
        val name: String,
        val threads: Int,
        val medianNs: Long,
        val score: Double,
        val valid: Boolean,
        val error: String?,
        val runs: List<RunEntry>,
    )

    /** 1 回の計測の内訳 (ウォームアップ含む)。 */
    data class RunEntry(
        val warmup: Boolean,
        val ns: Long,
        val cpus: List<Int>,
        val ghzBefore: Double,   // シングル: そのスレッド / マルチ: 全ワーカーの平均
        val ghzAfter: Double,
        val unitsPerWorker: List<Int>,
        val unitNs: List<Long>,  // シングルのみ
    )

    data class Topology(
        val maxKhz: List<Long>,
        val capacity: List<Int>,
        val caches: List<String>,
    )

    data class SweepPoint(
        val n: Int,
        val threads: Int,
        val stWorkingSetBytes: Long,
        val mtWorkingSetBytes: Long,
        val stGflops: Double,
        val mtGflops: Double,
        val valid: Boolean,
    )

    companion object {
        fun parse(json: String): BenchmarkResult {
            val root = JSONObject(json)
            root.optString("error").takeIf { it.isNotEmpty() }?.let { throw IllegalStateException(it) }
            val buildObj = root.getJSONObject("build")
            val build = buildObj.keys().asSequence().associateWith { buildObj.get(it).toString() }
            val testsArr = root.getJSONArray("tests")
            val tests = (0 until testsArr.length()).map { i ->
                val t = testsArr.getJSONObject(i)
                TestEntry(
                    id = t.getString("id"),
                    name = t.getString("name"),
                    threads = t.getInt("threads"),
                    medianNs = t.getLong("median_ns"),
                    score = t.getDouble("score"),
                    valid = t.getBoolean("valid"),
                    error = t.optString("error").takeIf { it.isNotEmpty() },
                    runs = t.optJSONArray("run_details")?.objects()?.map(::parseRun).orEmpty(),
                )
            }
            val diag = root.optJSONObject("diagnostics")
            return BenchmarkResult(
                benchmarkVersion = root.getString("benchmark_version"),
                overallScore = root.getDouble("overall_score"),
                valid = root.getBoolean("valid"),
                cancelled = root.getBoolean("cancelled"),
                logicalCores = root.getJSONObject("config").getInt("logical_cores"),
                build = build,
                tests = tests,
                topology = parseTopology(root.optJSONObject("cpu_topology")),
                sweep = diag?.optJSONArray("matmul_sweep")?.objects()?.map(::parseSweep).orEmpty(),
                freqSampleCount = diag?.optJSONObject("freq_samples")?.optJSONArray("t_ms")?.length() ?: 0,
                summaryText = root.optString("summary_text"),
            )
        }

        private fun parseRun(r: JSONObject): RunEntry {
            val workers = r.optJSONArray("workers")?.objects().orEmpty()
            val unitCpus = r.optJSONArray("unit_cpus")?.ints().orEmpty()
            val cpus = (unitCpus + workers.flatMap { it.optJSONArray("cpus")?.ints().orEmpty() })
                .filter { it >= 0 }.distinct().sorted()
            fun avg(key: String) = workers.map { it.optDouble(key, 0.0) }.filter { it > 0 }
                .takeIf { it.isNotEmpty() }?.average() ?: 0.0
            return RunEntry(
                warmup = r.optBoolean("warmup"),
                ns = r.optLong("ns"),
                cpus = cpus,
                ghzBefore = avg("ghz_before"),
                ghzAfter = avg("ghz_after"),
                unitsPerWorker = workers.map { it.optInt("units") },
                unitNs = r.optJSONArray("unit_ns")?.longs().orEmpty(),
            )
        }

        private fun parseTopology(t: JSONObject?): Topology {
            if (t == null) return Topology(emptyList(), emptyList(), emptyList())
            val caches = t.optJSONArray("caches")?.objects().orEmpty()
                .filter { it.optInt("level") >= 2 }
                .map { c -> "L${c.optInt("level")} ${formatBytes(c.optLong("size_bytes"))} (cpu ${c.optString("shared_cpus")})" }
            return Topology(
                maxKhz = t.optJSONArray("max_khz")?.longs().orEmpty(),
                capacity = t.optJSONArray("capacity")?.ints().orEmpty(),
                caches = caches,
            )
        }

        private fun parseSweep(p: JSONObject) = SweepPoint(
            n = p.optInt("n"),
            threads = p.optInt("threads"),
            stWorkingSetBytes = p.optLong("st_working_set_bytes"),
            mtWorkingSetBytes = p.optLong("mt_working_set_bytes"),
            stGflops = p.optDouble("st_gflops", 0.0),
            mtGflops = p.optDouble("mt_gflops", 0.0),
            valid = p.optBoolean("valid"),
        )

        fun formatBytes(bytes: Long): String = when {
            bytes < 0 -> "?"
            bytes >= 1024L * 1024 -> "%.1fMB".format(java.util.Locale.ROOT, bytes / 1024.0 / 1024.0)
            else -> "${bytes / 1024}KB"
        }

        private fun JSONArray.objects(): List<JSONObject> = (0 until length()).map { getJSONObject(it) }
        private fun JSONArray.ints(): List<Int> = (0 until length()).map { getInt(it) }
        private fun JSONArray.longs(): List<Long> = (0 until length()).map { getLong(it) }
    }
}
