package com.mobilebench.app

import android.app.Application
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Build
import android.os.PowerManager
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.mobilebench.app.bench.BenchmarkResult
import com.mobilebench.app.bench.ConditionProvider
import com.mobilebench.app.bench.NativeBench
import com.mobilebench.app.bench.ProgressListener
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

sealed interface UiState {
    data object Idle : UiState
    data class Running(
        val fraction: Float,
        val testName: String,
        val testIndex: Int,
        val testCount: Int,
        val warmup: Boolean,
        val phase: String = "",
        val waitMs: Int = -1,
        val attempt: Int = 1,
    ) : UiState
    data class Done(val result: BenchmarkResult, val rawJson: String) : UiState
    data class Failed(val message: String, val rawJson: String?) : UiState
}

class BenchmarkViewModel(application: Application) : AndroidViewModel(application) {
    private val _state = MutableStateFlow<UiState>(UiState.Idle)
    val state: StateFlow<UiState> = _state.asStateFlow()

    /** 診断モード (行列サイズ掃引・周波数の定期記録)。スコアには影響しない。 */
    private val _diagnostics = MutableStateFlow(false)
    val diagnostics: StateFlow<Boolean> = _diagnostics.asStateFlow()

    fun setDiagnostics(enabled: Boolean) {
        if (_state.value !is UiState.Running) _diagnostics.value = enabled
    }

    @Volatile
    private var current: NativeBench? = null

    fun start() {
        if (_state.value is UiState.Running) return
        _state.value = UiState.Running(0f, "", 0, 0, true)
        val diagnosticsEnabled = _diagnostics.value
        viewModelScope.launch {
            // 計測は UI スレッドをブロックしないよう Default ディスパッチャ (ワーカースレッド) で実行
            val json = withContext(Dispatchers.Default) {
                runCatching {
                    NativeBench(
                        deviceInfo(),
                        getApplication<Application>().cacheDir.absolutePath,
                        diagnosticsEnabled,
                    ).use { bench ->
                        current = bench
                        try {
                            val listener = ProgressListener {
                                    step, total, testIndex, testCount, testName, warmup, phase, waitMs, attempt ->
                                // StateFlow はスレッドセーフなのでバックグラウンドから直接更新してよい
                                _state.value = UiState.Running(
                                    fraction = if (total > 0) (step.toFloat() / total).coerceAtMost(1f) else 0f,
                                    testName = testName,
                                    testIndex = testIndex,
                                    testCount = testCount,
                                    warmup = warmup,
                                    phase = phase,
                                    waitMs = waitMs,
                                    attempt = attempt,
                                )
                            }
                            bench.run(listener, ConditionProvider { currentConditions() })
                        } finally {
                            current = null
                        }
                    }
                }
            }
            _state.value = json.fold(
                onSuccess = { raw ->
                    runCatching { UiState.Done(BenchmarkResult.parse(raw), raw) }
                        .getOrElse { UiState.Failed(it.message ?: "JSON parse error", raw) }
                },
                onFailure = { UiState.Failed(it.message ?: it.toString(), null) },
            )
        }
    }

    fun cancel() {
        current?.cancel()
    }

    override fun onCleared() {
        cancel()
        super.onCleared()
    }

    /**
     * 端末の状態を Android の公式 API で取得する (各テストの開始・終了時に計測スレッドから呼ばれる)。
     * - thermal_status: PowerManager の発熱状態 (NONE が通常)
     * - thermal_headroom_10s: 10 秒後に発熱制限に達するまでの余裕 (1.0 で制限開始。短い間隔で呼ぶと NaN)
     * - power_save: 省電力モード
     * - battery_temp_c / battery_level / charging: 電池の状態
     */
    private fun currentConditions(): Array<String> {
        val app = getApplication<Application>()
        val out = mutableListOf<String>()
        fun put(key: String, value: Any?) {
            out += key
            out += value.toString()
        }
        val pm = app.getSystemService(PowerManager::class.java)
        if (pm != null) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) put("thermal_status", thermalName(pm.currentThermalStatus))
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val headroom = pm.getThermalHeadroom(10)
                put("thermal_headroom_10s", if (headroom.isNaN()) "NaN" else String.format(Locale.ROOT, "%.2f", headroom))
            }
            put("power_save", pm.isPowerSaveMode)
        }
        // ACTION_BATTERY_CHANGED は sticky なので receiver なしで最新値を取得できる
        runCatching { app.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED)) }.getOrNull()?.let { battery ->
            val temp = battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Int.MIN_VALUE)
            if (temp != Int.MIN_VALUE) put("battery_temp_c", String.format(Locale.ROOT, "%.1f", temp / 10.0))
            val level = battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
            val scale = battery.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
            if (level >= 0 && scale > 0) put("battery_level", level * 100 / scale)
            put("charging", battery.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0)
        }
        return out.toTypedArray()
    }

    private fun thermalName(status: Int): String = when (status) {
        PowerManager.THERMAL_STATUS_NONE -> "NONE"
        PowerManager.THERMAL_STATUS_LIGHT -> "LIGHT"
        PowerManager.THERMAL_STATUS_MODERATE -> "MODERATE"
        PowerManager.THERMAL_STATUS_SEVERE -> "SEVERE"
        PowerManager.THERMAL_STATUS_CRITICAL -> "CRITICAL"
        PowerManager.THERMAL_STATUS_EMERGENCY -> "EMERGENCY"
        PowerManager.THERMAL_STATUS_SHUTDOWN -> "SHUTDOWN"
        else -> "UNKNOWN($status)"
    }

    private fun deviceInfo(): Map<String, String> = buildMap {
        put("platform", "android")
        put("manufacturer", Build.MANUFACTURER)
        put("model", Build.MODEL)
        put("os_version", Build.VERSION.RELEASE)
        put("sdk_int", Build.VERSION.SDK_INT.toString())
        put("abi", Build.SUPPORTED_ABIS.firstOrNull() ?: "unknown")
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            put("soc_manufacturer", Build.SOC_MANUFACTURER)
            put("soc_model", Build.SOC_MODEL)
        }
        put("app_version", BuildConfig.VERSION_NAME)
        put("app_build", BuildConfig.VERSION_CODE.toString())
        put("git_sha", BuildConfig.GIT_SHA)
    }
}
