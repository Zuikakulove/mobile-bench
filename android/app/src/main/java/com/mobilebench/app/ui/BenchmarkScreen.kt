package com.mobilebench.app.ui

import android.content.Intent
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.mobilebench.app.BenchmarkViewModel
import com.mobilebench.app.BuildConfig
import com.mobilebench.app.R
import com.mobilebench.app.UiState
import com.mobilebench.app.bench.BenchmarkResult
import java.util.Locale

@Composable
fun BenchmarkScreen(viewModel: BenchmarkViewModel) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val diagnostics by viewModel.diagnostics.collectAsStateWithLifecycle()
    val running = state is UiState.Running

    // 計測中は画面を消さない (スリープで計測が中断・減速しないように)
    val view = LocalView.current
    DisposableEffect(running) {
        view.keepScreenOn = running
        onDispose { view.keepScreenOn = false }
    }

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Spacer(Modifier.height(8.dp))
            Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineMedium)
            Text(
                stringResource(
                    R.string.build_label,
                    BuildConfig.VERSION_NAME,
                    BuildConfig.VERSION_CODE,
                    BuildConfig.GIT_SHA,
                ),
                style = MaterialTheme.typography.bodySmall,
            )

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = viewModel::start, enabled = !running) {
                    Text(stringResource(R.string.start))
                }
                if (running) {
                    OutlinedButton(onClick = viewModel::cancel) { Text(stringResource(R.string.cancel)) }
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Switch(checked = diagnostics, onCheckedChange = viewModel::setDiagnostics, enabled = !running)
                Text(
                    stringResource(R.string.diagnostics_label),
                    modifier = Modifier.padding(start = 8.dp),
                    style = MaterialTheme.typography.bodyMedium,
                )
            }

            when (val s = state) {
                UiState.Idle -> Text(stringResource(R.string.idle_hint))
                is UiState.Running -> ProgressSection(s)
                is UiState.Done -> ResultSection(s.result, s.rawJson)
                is UiState.Failed -> {
                    Text(
                        stringResource(R.string.failed, s.message),
                        color = MaterialTheme.colorScheme.error,
                    )
                    s.rawJson?.let { RawJson(it) }
                }
            }
            Spacer(Modifier.height(16.dp))
        }
    }
}

@Composable
private fun ProgressSection(s: UiState.Running) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        LinearProgressIndicator(progress = { s.fraction }, modifier = Modifier.fillMaxWidth())
        Text(String.format(Locale.ROOT, "%d%%", (s.fraction * 100).toInt()))
        if (s.testName.isNotEmpty()) {
            val phase = stringResource(if (s.warmup) R.string.phase_warmup else R.string.phase_measure)
            val index = if (s.testIndex < s.testCount) "(${s.testIndex + 1}/${s.testCount}) " else ""
            Text("$index${s.testName}  [$phase]")
        }
        Text(stringResource(R.string.running_hint), style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun ResultSection(result: BenchmarkResult, rawJson: String) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
    ) {
        Column(Modifier.padding(16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(stringResource(R.string.overall_score), style = MaterialTheme.typography.titleMedium)
            Text(
                formatScore(result.overallScore),
                fontSize = 48.sp,
                fontWeight = FontWeight.Bold,
            )
            Text("benchmark v${result.benchmarkVersion}", style = MaterialTheme.typography.bodySmall)
            if (!result.valid) {
                Text(
                    stringResource(if (result.cancelled) R.string.cancelled else R.string.invalid),
                    color = MaterialTheme.colorScheme.error,
                )
            }
        }
    }

    JsonActions(rawJson, result.summaryText)

    result.tests.forEach { t -> TestCard(t) }

    if (result.sweep.isNotEmpty()) SweepTable(result)

    HorizontalDivider()
    Text(
        (result.build.entries.map { "${it.key}: ${it.value}" } +
            "logical_cores: ${result.logicalCores}" +
            topologyLines(result.topology)).joinToString("\n"),
        style = MaterialTheme.typography.bodySmall,
    )
    RawJson(rawJson)
}

@Composable
private fun TestCard(t: BenchmarkResult.TestEntry) {
    var expanded by rememberSaveable(t.id) { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(t.name, style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
                Text(formatScore(t.score), style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            }
            Text(
                String.format(
                    Locale.ROOT,
                    "median %.1f ms / threads %d%s",
                    t.medianNs / 1e6,
                    t.threads,
                    if (t.valid) "" else " / INVALID",
                ),
                style = MaterialTheme.typography.bodySmall,
            )
            t.error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            if (t.runs.isNotEmpty()) {
                TextButton(onClick = { expanded = !expanded }, modifier = Modifier.padding(0.dp)) {
                    Text(stringResource(if (expanded) R.string.hide_runs else R.string.show_runs))
                }
                if (expanded) {
                    Text(
                        t.runs.mapIndexed { i, r -> formatRun(i, r, t.threads > 1) }.joinToString("\n"),
                        fontFamily = FontFamily.Monospace,
                        fontSize = 11.sp,
                    )
                }
            }
        }
    }
}

@Composable
private fun SweepTable(result: BenchmarkResult) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(stringResource(R.string.sweep_title), style = MaterialTheme.typography.titleSmall)
            Text(stringResource(R.string.sweep_hint), style = MaterialTheme.typography.bodySmall)
            val header = String.format(Locale.ROOT, "%5s %9s %9s %7s %7s %6s", "n", "WS(1T)", "WS(MT)", "1T GF", "MT GF", "MT/1T")
            val rows = result.sweep.map { p ->
                String.format(
                    Locale.ROOT,
                    "%5d %9s %9s %7.2f %7.2f %5.2fx%s",
                    p.n,
                    BenchmarkResult.formatBytes(p.stWorkingSetBytes),
                    BenchmarkResult.formatBytes(p.mtWorkingSetBytes),
                    p.stGflops,
                    p.mtGflops,
                    if (p.stGflops > 0) p.mtGflops / p.stGflops else 0.0,
                    if (p.valid) "" else " !",
                )
            }
            Text((listOf(header) + rows).joinToString("\n"), fontFamily = FontFamily.Monospace, fontSize = 11.sp)
            Text(
                stringResource(R.string.freq_samples, result.freqSampleCount),
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}

@Composable
private fun JsonActions(rawJson: String, summary: String) {
    val clipboard = LocalClipboardManager.current
    val context = LocalContext.current
    val copiedMessage = stringResource(R.string.copied)
    // 要約 (数 KB) はチャットに貼っても切れない。JSON 全体は大きいので共有 (ファイル送信等) 向け
    if (summary.isNotEmpty()) {
        Button(onClick = {
            clipboard.setText(AnnotatedString(summary))
            Toast.makeText(context, copiedMessage, Toast.LENGTH_SHORT).show()
        }) { Text(stringResource(R.string.copy_summary)) }
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedButton(onClick = {
            clipboard.setText(AnnotatedString(rawJson))
            Toast.makeText(context, copiedMessage, Toast.LENGTH_SHORT).show()
        }) { Text(stringResource(R.string.copy_json)) }
        OutlinedButton(onClick = {
            val send = Intent(Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(Intent.EXTRA_SUBJECT, "MobileBench result")
                putExtra(Intent.EXTRA_TEXT, rawJson)
            }
            context.startActivity(Intent.createChooser(send, null))
        }) { Text(stringResource(R.string.share_json)) }
    }
}

@Composable
private fun RawJson(json: String) {
    var expanded by rememberSaveable { mutableStateOf(false) }
    TextButton(onClick = { expanded = !expanded }) {
        Text(stringResource(if (expanded) R.string.hide_json else R.string.show_json))
    }
    if (expanded) {
        SelectionContainer {
            Text(json, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
        }
    }
}

/** 例: "#3  109.4ms cpu7     4.31→4.32GHz [27.1 27.4 27.5 27.4]" */
private fun formatRun(index: Int, r: BenchmarkResult.RunEntry, multi: Boolean): String {
    val label = if (r.warmup) "W " else "#$index"
    val cpus = if (r.cpus.isEmpty()) "cpu?" else "cpu" + compressRanges(r.cpus)
    val ghz = if (r.ghzAfter > 0) String.format(Locale.ROOT, "%.2f→%.2fGHz", r.ghzBefore, r.ghzAfter) else ""
    val tail = if (multi) {
        if (r.unitsPerWorker.isEmpty()) "" else "units ${r.unitsPerWorker.min()}-${r.unitsPerWorker.max()}"
    } else {
        r.unitNs.joinToString(" ", "[", "]") { String.format(Locale.ROOT, "%.1f", it / 1e6) }
    }
    return String.format(Locale.ROOT, "%-3s %7.1fms %-8s %s %s", label, r.ns / 1e6, cpus, ghz, tail)
}

/** [0,1,2,3,6,7] -> "0-3,6-7" */
private fun compressRanges(values: List<Int>): String {
    if (values.isEmpty()) return ""
    val parts = mutableListOf<String>()
    var start = values[0]
    var prev = values[0]
    for (v in values.drop(1) + Int.MIN_VALUE) {
        if (v == prev + 1) {
            prev = v
            continue
        }
        parts += if (start == prev) "$start" else "$start-$prev"
        start = v
        prev = v
    }
    return parts.joinToString(",")
}

private fun topologyLines(t: BenchmarkResult.Topology): List<String> {
    val lines = mutableListOf<String>()
    if (t.maxKhz.any { it > 0 }) {
        lines += "max_mhz: " + t.maxKhz.joinToString(",") { if (it > 0) (it / 1000).toString() else "?" }
    }
    if (t.capacity.any { it > 0 }) lines += "capacity: " + t.capacity.joinToString(",")
    lines += t.caches
    return lines
}

private fun formatScore(score: Double): String = String.format(Locale.ROOT, "%.0f", score)
