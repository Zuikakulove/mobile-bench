package com.mobilebench.app.bench

/** core からの進捗通知。計測スレッド (バックグラウンド) から呼ばれる。 */
fun interface ProgressListener {
    fun onProgress(step: Int, totalSteps: Int, testIndex: Int, testCount: Int, testName: String, warmup: Boolean)
}

/** JNI の宣言 (実装は src/main/cpp/jni_bridge.cpp)。 */
object NativeLib {
    init {
        System.loadLibrary("mobilebench_jni")
    }

    external fun nativeCreate(deviceKeys: Array<String>, deviceValues: Array<String>, scratchDir: String): Long
    external fun nativeRun(handle: Long, listener: ProgressListener?): ByteArray
    external fun nativeCancel(handle: Long)
    external fun nativeDestroy(handle: Long)
}

/**
 * core の Runner 1 回分のラッパー。
 * [run] は呼び出しスレッドをブロックするので、必ずバックグラウンドから呼ぶこと。
 * [cancel] は任意のスレッドから呼んでよい。
 */
class NativeBench(device: Map<String, String>, scratchDir: String) : AutoCloseable {
    private val lock = Any()
    private var handle: Long = NativeLib.nativeCreate(
        device.keys.toTypedArray(),
        device.values.toTypedArray(),
        scratchDir,
    )

    /** 計測を実行し、core が生成した JSON を返す。 */
    fun run(listener: ProgressListener?): String {
        val h = synchronized(lock) { handle }
        check(h != 0L) { "already closed" }
        return NativeLib.nativeRun(h, listener).toString(Charsets.UTF_8)
    }

    fun cancel() {
        synchronized(lock) {
            if (handle != 0L) NativeLib.nativeCancel(handle)
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
