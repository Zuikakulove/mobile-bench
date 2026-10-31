package com.mobilebench.app.ui

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
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.mobilebench.app.BenchmarkViewModel
import com.mobilebench.app.R
import com.mobilebench.app.UiState
import com.mobilebench.app.bench.BenchmarkResult
import java.util.Locale

@Composable
fun BenchmarkScreen(viewModel: BenchmarkViewModel) {
    val state by viewModel.state.collectAsStateWithLifecycle()
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

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = viewModel::start, enabled = !running) {
                    Text(stringResource(R.string.start))
                }
                if (running) {
                    OutlinedButton(onClick = viewModel::cancel) { Text(stringResource(R.string.cancel)) }
                }
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
        if (s.testCount > 0) {
            val phase = stringResource(if (s.warmup) R.string.phase_warmup else R.string.phase_measure)
            Text("(${s.testIndex + 1}/${s.testCount}) ${s.testName}  [$phase]")
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
            Text("v${result.benchmarkVersion}", style = MaterialTheme.typography.bodySmall)
            if (!result.valid) {
                Text(
                    stringResource(if (result.cancelled) R.string.cancelled else R.string.invalid),
                    color = MaterialTheme.colorScheme.error,
                )
            }
        }
    }

    result.tests.forEach { t ->
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
            }
        }
    }

    HorizontalDivider()
    Text(
        result.build.entries.joinToString("\n") { "${it.key}: ${it.value}" } +
            "\nlogical_cores: ${result.logicalCores}",
        style = MaterialTheme.typography.bodySmall,
    )
    RawJson(rawJson)
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

private fun formatScore(score: Double): String = String.format(Locale.ROOT, "%.0f", score)
