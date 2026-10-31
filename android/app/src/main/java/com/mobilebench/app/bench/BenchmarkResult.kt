package com.mobilebench.app.bench

import org.json.JSONObject

/**
 * core が返す JSON (schema_version 1) の表示用モデル。
 * スコア計算などは一切行わず、JSON の値をそのまま読むだけ。
 */
data class BenchmarkResult(
    val benchmarkVersion: String,
    val overallScore: Double,
    val valid: Boolean,
    val cancelled: Boolean,
    val logicalCores: Int,
    val build: Map<String, String>,
    val tests: List<TestEntry>,
) {
    data class TestEntry(
        val id: String,
        val name: String,
        val threads: Int,
        val medianNs: Long,
        val runsNs: List<Long>,
        val score: Double,
        val valid: Boolean,
        val error: String?,
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
                val runs = t.getJSONArray("runs_ns")
                TestEntry(
                    id = t.getString("id"),
                    name = t.getString("name"),
                    threads = t.getInt("threads"),
                    medianNs = t.getLong("median_ns"),
                    runsNs = (0 until runs.length()).map { runs.getLong(it) },
                    score = t.getDouble("score"),
                    valid = t.getBoolean("valid"),
                    error = t.optString("error").takeIf { it.isNotEmpty() },
                )
            }
            return BenchmarkResult(
                benchmarkVersion = root.getString("benchmark_version"),
                overallScore = root.getDouble("overall_score"),
                valid = root.getBoolean("valid"),
                cancelled = root.getBoolean("cancelled"),
                logicalCores = root.getJSONObject("config").getInt("logical_cores"),
                build = build,
                tests = tests,
            )
        }
    }
}
