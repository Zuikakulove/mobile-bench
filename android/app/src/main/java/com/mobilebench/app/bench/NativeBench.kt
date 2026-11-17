package com.mobilebench.app.bench

/**
 * core からの進捗通知。計測スレッド (バックグラウンド) から呼ばれる。
 * phase: "prepare" / "peak" / "sustained" / "diagnostics"
 * waitMs: 回復待ち・事前負荷の経過時間 (それ以外は -1)、attempt: 測り直しの回数 (1 始まり)
 */
fun interface ProgressListener {
    fun onProgress(
        step: Int,
        totalSteps: Int,
        testIndex: Int,
        testCount: Int,
        testName: String,
        warmup: Boolean,
        phase: String,
        waitMs: Int,
        attempt: Int,
    )
}

/**
 * 端末の状態 (発熱状態・省電力・電池温度など) を返す。各テストの開始・終了時に計測スレッドから呼ばれる。
 * 戻り値は [key0, value0, key1, value1, ...] の形。
 */
fun interface ConditionProvider {
    fun conditions(): Array<String>
}

/** JNI の宣言 (実装は src/main/cpp/jni_bridge.cpp)。 */
object NativeLib {
    /** [nativeInterrupt] の理由: 計測中にアプリが前面を離れた (core の mb::kInterruptLeftForeground と同じ) */
    const val INTERRUPT_LEFT_FOREGROUND = "left_foreground"

    init {
        System.loadLibrary("mobilebench_jni")
    }

    external fun nativeCreate(
        deviceKeys: Array<String>,
        deviceValues: Array<String>,
        scratchDir: String,
        diagnostics: Boolean,
    ): Long
    external fun nativeRun(handle: Long, listener: ProgressListener?, conditionProvider: ConditionProvider?): ByteArray
    external fun nativeCancel(handle: Long)
    external fun nativeInterrupt(handle: Long, reason: String)
    external fun nativeNoteTouch(handle: Long)
    external fun nativeDestroy(handle: Long)
}

/**
 * core の Runner 1 回分のラッパー。
 * [run] は呼び出しスレッドをブロックするので、必ずバックグラウンドから呼ぶこと。
 * [cancel]、[interrupt]、[noteTouch] は任意のスレッドから呼んでよい。
 */
class NativeBench(device: Map<String, String>, scratchDir: String, diagnostics: Boolean) : AutoCloseable {
    private val lock = Any()
    private var handle: Long = NativeLib.nativeCreate(
        device.keys.toTypedArray(),
        device.values.toTypedArray(),
        scratchDir,
        diagnostics,
    )

    /** 計測を実行し、core が生成した JSON を返す。 */
    fun run(listener: ProgressListener?, conditionProvider: ConditionProvider? = null): String {
        val h = synchronized(lock) { handle }
        check(h != 0L) { "already closed" }
        return NativeLib.nativeRun(h, listener, conditionProvider).toString(Charsets.UTF_8)
    }

    fun cancel() {
        synchronized(lock) {
            if (handle != 0L) NativeLib.nativeCancel(handle)
        }
    }

    /** 計測を中止し、結果を無効にする (利用者の中止 [cancel] とは区別して記録される)。 */
    fun interrupt(reason: String) {
        synchronized(lock) {
            if (handle != 0L) NativeLib.nativeInterrupt(handle, reason)
        }
    }

    /** 画面へのタッチを記録する (計測中だけ記録される。判定には使わない)。 */
    fun noteTouch() {
        synchronized(lock) {
            if (handle != 0L) NativeLib.nativeNoteTouch(handle)
        }
    }

    override fun close() {
        synchronized(lock) {
            if (handle != 0L) {
                NativeLib.nativeDestroy(handle)
                handle = 0L
            }
        }
    }
}
