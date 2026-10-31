package com.mobilebench.app

import android.app.Application
import android.os.Build
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.mobilebench.app.bench.BenchmarkResult
import com.mobilebench.app.bench.NativeBench
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

sealed interface UiState {
    data object Idle : UiState
    data class Running(
        val fraction: Float,
        val testName: String,
        val testIndex: Int,
        val testCount: Int,
        val warmup: Boolean,
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
                            bench.run { step, total, testIndex, testCount, testName, warmup ->
                                // StateFlow はスレッドセーフなのでバックグラウンドから直接更新してよい
                                _state.value = UiState.Running(
                                    fraction = if (total > 0) step.toFloat() / total else 0f,
                                    testName = testName,
                                    testIndex = testIndex,
                                    testCount = testCount,
                                    warmup = warmup,
                                )
                            }
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
