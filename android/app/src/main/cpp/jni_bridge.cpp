// Kotlin (com.mobilebench.app.bench.NativeLib) と core を繋ぐ JNI ブリッジ。
// 計測ロジックは一切持たず、core の Runner を呼んで JSON を返すだけ。
#include <jni.h>

#include <exception>
#include <string>

#include "mobilebench/runner.h"

namespace {

std::string to_std_string(JNIEnv* env, jstring s) {
  if (s == nullptr) return {};
  const char* chars = env->GetStringUTFChars(s, nullptr);
  std::string out = chars ? chars : "";
  if (chars) env->ReleaseStringUTFChars(s, chars);
  return out;
}

jbyteArray to_byte_array(JNIEnv* env, const std::string& s) {
  jbyteArray arr = env->NewByteArray(static_cast<jsize>(s.size()));
  if (arr != nullptr) {
    env->SetByteArrayRegion(arr, 0, static_cast<jsize>(s.size()), reinterpret_cast<const jbyte*>(s.data()));
  }
  return arr;
}

std::string error_json(const std::string& message) {
  std::string escaped;
  for (char c : message) {
    if (c == '"' || c == '\\') escaped += '\\';
    escaped += (static_cast<unsigned char>(c) < 0x20) ? ' ' : c;
  }
  return "{\"error\":\"" + escaped + "\"}";
}

mb::Runner* from_handle(jlong handle) { return reinterpret_cast<mb::Runner*>(handle); }

}  // namespace

extern "C" JNIEXPORT jlong JNICALL Java_com_mobilebench_app_bench_NativeLib_nativeCreate(
    JNIEnv* env, jobject /*thiz*/, jobjectArray device_keys, jobjectArray device_values, jstring scratch_dir,
    jboolean diagnostics) {
  mb::RunConfig config;
  config.scratch_dir = to_std_string(env, scratch_dir);
  config.diagnostics = diagnostics == JNI_TRUE;
  const jsize n = env->GetArrayLength(device_keys);
  for (jsize i = 0; i < n; ++i) {
    auto k = static_cast<jstring>(env->GetObjectArrayElement(device_keys, i));
    auto v = static_cast<jstring>(env->GetObjectArrayElement(device_values, i));
    config.device[to_std_string(env, k)] = to_std_string(env, v);
    env->DeleteLocalRef(k);
    env->DeleteLocalRef(v);
  }
  return reinterpret_cast<jlong>(new mb::Runner(std::move(config)));
}

// 呼び出し元 (Kotlin のバックグラウンドスレッド) でブロックし、結果 JSON を UTF-8 で返す。
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_mobilebench_app_bench_NativeLib_nativeRun(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jobject listener, jobject condition_provider) {
  mb::Runner* runner = from_handle(handle);
  if (runner == nullptr) return to_byte_array(env, error_json("invalid handle"));

  // 端末の状態 (発熱状態など) は Kotlin 側が Android の公式 API で取得する。
  // 各テストの開始・終了時に、run() を呼んだのと同じスレッドで呼ばれる
  if (condition_provider != nullptr) {
    jclass cls = env->GetObjectClass(condition_provider);
    jmethodID get_conditions = env->GetMethodID(cls, "conditions", "()[Ljava/lang/String;");
    env->DeleteLocalRef(cls);
    runner->set_condition_probe([env, condition_provider, get_conditions]() {
      mb::Conditions out;
      auto arr = static_cast<jobjectArray>(env->CallObjectMethod(condition_provider, get_conditions));
      if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        out["error"] = "condition provider threw";
        return out;
      }
      if (arr == nullptr) return out;
      const jsize n = env->GetArrayLength(arr);
      for (jsize i = 0; i + 1 < n; i += 2) {  // [key0, value0, key1, value1, ...]
        auto k = static_cast<jstring>(env->GetObjectArrayElement(arr, i));
        auto v = static_cast<jstring>(env->GetObjectArrayElement(arr, i + 1));
        out[to_std_string(env, k)] = to_std_string(env, v);
        env->DeleteLocalRef(k);
        env->DeleteLocalRef(v);
      }
      env->DeleteLocalRef(arr);
      return out;
    });
  }

  jmethodID on_progress = nullptr;
  if (listener != nullptr) {
    jclass cls = env->GetObjectClass(listener);
    on_progress = env->GetMethodID(cls, "onProgress", "(IIIILjava/lang/String;ZLjava/lang/String;II)V");
    env->DeleteLocalRef(cls);
  }

  // コールバックは run() を呼んだのと同じスレッドで呼ばれるので env をそのまま使える
  auto callback = [&](const mb::Progress& p) {
    if (on_progress == nullptr) return;
    jstring name = env->NewStringUTF(p.test_name.c_str());
    jstring phase = env->NewStringUTF(p.phase.c_str());
    env->CallVoidMethod(listener, on_progress, p.step, p.total_steps, p.test_index, p.test_count, name,
                        p.warmup ? JNI_TRUE : JNI_FALSE, phase, p.wait_ms, p.attempt);
    env->DeleteLocalRef(phase);
    env->DeleteLocalRef(name);
    if (env->ExceptionCheck()) {
      env->ExceptionDescribe();
      env->ExceptionClear();
      runner->cancel();
    }
  };

  std::string json;
  try {
    json = runner->run_json(callback);
  } catch (const std::exception& e) {
    json = error_json(e.what());
  } catch (...) {
    json = error_json("unknown native error");
  }
  runner->set_condition_probe(nullptr);  // env と provider はこの呼び出しの間だけ有効
  return to_byte_array(env, json);
}

extern "C" JNIEXPORT void JNICALL Java_com_mobilebench_app_bench_NativeLib_nativeCancel(JNIEnv* /*env*/,
                                                                                        jobject /*thiz*/,
                                                                                        jlong handle) {
  if (auto* runner = from_handle(handle)) runner->cancel();
}

extern "C" JNIEXPORT void JNICALL Java_com_mobilebench_app_bench_NativeLib_nativeDestroy(JNIEnv* /*env*/,
                                                                                         jobject /*thiz*/,
                                                                                         jlong handle) {
  delete from_handle(handle);
}
