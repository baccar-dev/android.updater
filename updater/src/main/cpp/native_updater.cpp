#include <jni.h>
#include <string>
#include <cstdlib>
#include <android/log.h>

#define LOG_TAG "NativeUpdater"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Result codes returned to the JVM when the process is NOT terminated.
static const jint RESULT_UP_TO_DATE = 0;
static const jint RESULT_ERROR = -1;

namespace {

// Clears any pending JVM exception and logs it.
bool clearIfException(JNIEnv *env, const char *where) {
    if (env->ExceptionCheck()) {
        LOGE("JNI exception during: %s", where);
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }
    return false;
}

// Performs an HTTPS GET using the platform's java.net.HttpURLConnection via JNI
// up-calls. This gives us TLS for free without bundling a native HTTP stack.
// Returns true on success and fills `out` with the response body.
bool httpGet(JNIEnv *env, const std::string &url, std::string &out, jint &httpCode) {
    jclass urlClass = env->FindClass("java/net/URL");
    if (clearIfException(env, "FindClass URL") || urlClass == nullptr) return false;

    jmethodID urlCtor = env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V");
    if (clearIfException(env, "GetMethodID URL.<init>")) return false;

    jstring jurl = env->NewStringUTF(url.c_str());
    jobject urlObj = env->NewObject(urlClass, urlCtor, jurl);
    if (clearIfException(env, "new URL(...)") || urlObj == nullptr) return false;

    jmethodID openConnection =
            env->GetMethodID(urlClass, "openConnection", "()Ljava/net/URLConnection;");
    jobject conn = env->CallObjectMethod(urlObj, openConnection);
    if (clearIfException(env, "URL.openConnection()") || conn == nullptr) return false;

    jclass httpClass = env->FindClass("java/net/HttpURLConnection");
    if (clearIfException(env, "FindClass HttpURLConnection")) return false;

    jmethodID setRequestMethod =
            env->GetMethodID(httpClass, "setRequestMethod", "(Ljava/lang/String;)V");
    jstring get = env->NewStringUTF("GET");
    env->CallVoidMethod(conn, setRequestMethod, get);
    if (clearIfException(env, "setRequestMethod")) return false;

    jmethodID setConnectTimeout = env->GetMethodID(httpClass, "setConnectTimeout", "(I)V");
    env->CallVoidMethod(conn, setConnectTimeout, 10000);
    jmethodID setReadTimeout = env->GetMethodID(httpClass, "setReadTimeout", "(I)V");
    env->CallVoidMethod(conn, setReadTimeout, 10000);
    if (clearIfException(env, "set timeouts")) return false;

    jmethodID getResponseCode = env->GetMethodID(httpClass, "getResponseCode", "()I");
    httpCode = env->CallIntMethod(conn, getResponseCode);
    if (clearIfException(env, "getResponseCode")) return false;

    // Pick the input or error stream depending on the status code.
    const char *streamGetter = (httpCode >= 200 && httpCode < 300)
                               ? "getInputStream" : "getErrorStream";
    jmethodID getStream =
            env->GetMethodID(httpClass, streamGetter, "()Ljava/io/InputStream;");
    jobject stream = env->CallObjectMethod(conn, getStream);
    if (clearIfException(env, "getInputStream/getErrorStream") || stream == nullptr) return false;

    jclass isClass = env->FindClass("java/io/InputStream");
    jmethodID readMethod = env->GetMethodID(isClass, "read", "([B)I");
    jmethodID closeMethod = env->GetMethodID(isClass, "close", "()V");

    const jsize bufSize = 4096;
    jbyteArray buffer = env->NewByteArray(bufSize);

    while (true) {
        jint read = env->CallIntMethod(stream, readMethod, buffer);
        if (clearIfException(env, "InputStream.read")) {
            env->CallVoidMethod(stream, closeMethod);
            env->ExceptionClear();
            return false;
        }
        if (read <= 0) break;

        jbyte *bytes = env->GetByteArrayElements(buffer, nullptr);
        out.append(reinterpret_cast<char *>(bytes), static_cast<size_t>(read));
        env->ReleaseByteArrayElements(buffer, bytes, JNI_ABORT);
    }

    env->CallVoidMethod(stream, closeMethod);
    env->ExceptionClear();

    jmethodID disconnect = env->GetMethodID(httpClass, "disconnect", "()V");
    env->CallVoidMethod(conn, disconnect);
    env->ExceptionClear();

    return true;
}

// Dependency-free check for a JSON boolean field of the form "hasUpdate": true.
bool jsonBoolFieldIsTrue(const std::string &json, const std::string &key) {
    const std::string quotedKey = "\"" + key + "\"";
    size_t pos = json.find(quotedKey);
    if (pos == std::string::npos) return false;
    pos += quotedKey.size();

    auto skipWs = [&]() {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                     json[pos] == '\n' || json[pos] == '\r')) {
            pos++;
        }
    };

    skipWs();
    if (pos >= json.size() || json[pos] != ':') return false;
    pos++;
    skipWs();

    return json.compare(pos, 4, "true") == 0;
}

}  // namespace

// Fetches the update descriptor natively and, if the server reports
// "hasUpdate": true, terminates the process via abort(). Otherwise returns a
// status code to the caller.
extern "C" JNIEXPORT jint JNICALL
Java_androidx_appcompact_example_updater_NativeUpdater_checkForUpdatesAndEnforce(
        JNIEnv *env, jobject /* thiz */, jstring jendpoint) {
    if (jendpoint == nullptr) {
        return RESULT_ERROR;
    }

    const char *chars = env->GetStringUTFChars(jendpoint, nullptr);
    if (chars == nullptr) {
        return RESULT_ERROR;
    }
    std::string endpoint(chars);
    env->ReleaseStringUTFChars(jendpoint, chars);

    std::string body;
    jint httpCode = 0;
    if (!httpGet(env, endpoint, body, httpCode)) {
        LOGE("Update check failed (network/JNI error)");
        return RESULT_ERROR;
    }

    if (httpCode < 200 || httpCode >= 300) {
        LOGE("Update check returned HTTP %d", httpCode);
        return RESULT_ERROR;
    }

    if (jsonBoolFieldIsTrue(body, "hasUpdate")) {
        LOGI("hasUpdate=true -> enforcing update by aborting the process");
        abort();  // Terminates the app. Never returns.
    }

    LOGI("hasUpdate=false (HTTP %d) -> up to date", httpCode);
    return RESULT_UP_TO_DATE;
}
