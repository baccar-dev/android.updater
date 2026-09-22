#include <unistd.h>
#include <jni.h>
#include <string>
#include <vector>

static constexpr jint RESULT_UP_TO_DATE = 0;
static constexpr jint RESULT_ERROR = -1;

namespace {

// RAII wrapper for PushLocalFrame / PopLocalFrame
    class ScopedLocalFrame {
    public:
        ScopedLocalFrame(JNIEnv* env, jint capacity = 16) : env_(env) {
            pushed_ = (env_->PushLocalFrame(capacity) == 0);
        }

        ~ScopedLocalFrame() {
            if (pushed_ && env_) {
                env_->PopLocalFrame(result_);
            }
        }

        // Allows returning a JNI local reference safely out of the popped frame
        template <typename T>
        T escape(T result) {
            result_ = static_cast<jobject>(result);
            return result;
        }

        explicit operator bool() const { return pushed_; }

        ScopedLocalFrame(const ScopedLocalFrame&) = delete;
        ScopedLocalFrame& operator=(const ScopedLocalFrame&) = delete;

    private:
        JNIEnv* env_;
        bool pushed_ = false;
        jobject result_ = nullptr;
    };

// RAII helper to ensure HttpURLConnection.disconnect() is always called
    class ScopedHttpDisconnect {
    public:
        ScopedHttpDisconnect(JNIEnv* env, jobject conn, jmethodID disconnectMethod)
                : env_(env), conn_(conn), disconnectMethod_(disconnectMethod) {}

        ~ScopedHttpDisconnect() {
            if (env_ && conn_ && disconnectMethod_) {
                env_->CallVoidMethod(conn_, disconnectMethod_);

                if (env_->ExceptionCheck()) {
                    env_->ExceptionClear();
                }
            }
        }

        ScopedHttpDisconnect(const ScopedHttpDisconnect&) = delete;
        ScopedHttpDisconnect& operator=(const ScopedHttpDisconnect&) = delete;

    private:
        JNIEnv* env_;
        jobject conn_;
        jmethodID disconnectMethod_;
    };

// RAII helper to ensure InputStream.close() is always called
    class ScopedInputStream {
    public:
        ScopedInputStream(JNIEnv* env, jobject stream, jmethodID closeMethod)
                : env_(env), stream_(stream), closeMethod_(closeMethod) {}

        ~ScopedInputStream() {
            if (env_ && stream_ && closeMethod_) {
                env_->CallVoidMethod(stream_, closeMethod_);

                if (env_->ExceptionCheck()) {
                    env_->ExceptionClear();
                }
            }
        }

        ScopedInputStream(const ScopedInputStream&) = delete;
        ScopedInputStream& operator=(const ScopedInputStream&) = delete;

    private:
        JNIEnv* env_;
        jobject stream_;
        jmethodID closeMethod_;
    };

// RAII wrapper for JNI String UTF Chars
    class ScopedStringUTFChars {
    public:
        ScopedStringUTFChars(JNIEnv* env, jstring jstr)
                : env_(env),
                  jstr_(jstr),
                  chars_(jstr ? env->GetStringUTFChars(jstr, nullptr) : nullptr) {}

        ~ScopedStringUTFChars() {
            if (chars_ && jstr_ && env_) {
                env_->ReleaseStringUTFChars(jstr_, chars_);
            }
        }

        const char* get() const { return chars_; }
        explicit operator bool() const { return chars_ != nullptr; }

        ScopedStringUTFChars(const ScopedStringUTFChars&) = delete;
        ScopedStringUTFChars& operator=(const ScopedStringUTFChars&) = delete;

    private:
        JNIEnv* env_;
        jstring jstr_;
        const char* chars_;
    };

    bool checkAndClearException(JNIEnv* env) {
        if (env->ExceptionCheck()) {
            jthrowable exc = env->ExceptionOccurred();
            env->ExceptionClear();

            if (exc) {
                env->DeleteLocalRef(exc);
            }

            return true;
        }

        return false;
    }

    bool jsonContainsTrueKey(
            JNIEnv* env,
            const std::string& jsonStr,
            const char* keyName) {

        ScopedLocalFrame frame(env, 16);
        if (!frame) return false;

        jclass jsonClass = env->FindClass("org/json/JSONObject");
        if (!jsonClass || checkAndClearException(env)) {
            return false;
        }

        jmethodID jsonCtor =
                env->GetMethodID(jsonClass, "<init>", "(Ljava/lang/String;)V");

        if (!jsonCtor || checkAndClearException(env)) {
            return false;
        }

        jmethodID optBoolean =
                env->GetMethodID(
                        jsonClass,
                        "optBoolean",
                        "(Ljava/lang/String;Z)Z");

        if (!optBoolean || checkAndClearException(env)) {
            return false;
        }

        jstring jJsonStr = env->NewStringUTF(jsonStr.c_str());
        if (!jJsonStr || checkAndClearException(env)) {
            return false;
        }

        jobject jsonObj = env->NewObject(jsonClass, jsonCtor, jJsonStr);
        if (!jsonObj || checkAndClearException(env)) {
            return false;
        }

        jstring jKey = env->NewStringUTF(keyName);
        if (!jKey || checkAndClearException(env)) {
            return false;
        }

        jboolean result =
                env->CallBooleanMethod(jsonObj, optBoolean, jKey, JNI_FALSE);

        if (checkAndClearException(env)) {
            return false;
        }

        return result == JNI_TRUE;
    }

    bool performHttpGet(
            JNIEnv* env,
            const std::string& urlStr,
            std::string& outResponse,
            jint& httpCode) {

        ScopedLocalFrame frame(env, 32);
        if (!frame) return false;

        jclass urlClass = env->FindClass("java/net/URL");
        if (!urlClass || checkAndClearException(env)) {
            return false;
        }

        jmethodID urlCtor =
                env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V");

        if (!urlCtor || checkAndClearException(env)) {
            return false;
        }

        jmethodID openConnMethod =
                env->GetMethodID(
                        urlClass,
                        "openConnection",
                        "()Ljava/net/URLConnection;");

        if (!openConnMethod || checkAndClearException(env)) {
            return false;
        }

        jstring jurl = env->NewStringUTF(urlStr.c_str());
        if (!jurl || checkAndClearException(env)) {
            return false;
        }

        jobject urlObj = env->NewObject(urlClass, urlCtor, jurl);
        if (!urlObj || checkAndClearException(env)) {
            return false;
        }

        jobject conn = env->CallObjectMethod(urlObj, openConnMethod);
        if (!conn || checkAndClearException(env)) {
            return false;
        }

        jclass httpClass =
                env->FindClass("java/net/HttpURLConnection");

        if (!httpClass || checkAndClearException(env)) {
            return false;
        }

        jmethodID disconnectMethod =
                env->GetMethodID(httpClass, "disconnect", "()V");

        if (!disconnectMethod || checkAndClearException(env)) {
            return false;
        }

        ScopedHttpDisconnect disconnectGuard(
                env,
                conn,
                disconnectMethod);

        jmethodID setReqMethod =
                env->GetMethodID(
                        httpClass,
                        "setRequestMethod",
                        "(Ljava/lang/String;)V");

        jmethodID setConnectTimeout =
                env->GetMethodID(
                        httpClass,
                        "setConnectTimeout",
                        "(I)V");

        jmethodID setReadTimeout =
                env->GetMethodID(
                        httpClass,
                        "setReadTimeout",
                        "(I)V");

        jmethodID getResponseCode =
                env->GetMethodID(
                        httpClass,
                        "getResponseCode",
                        "()I");

        if (!setReqMethod ||
            !setConnectTimeout ||
            !setReadTimeout ||
            !getResponseCode ||
            checkAndClearException(env)) {
            return false;
        }

        jstring getStr = env->NewStringUTF("GET");
        if (!getStr || checkAndClearException(env)) {
            return false;
        }

        env->CallVoidMethod(conn, setReqMethod, getStr);
        if (checkAndClearException(env)) {
            return false;
        }

        env->CallVoidMethod(conn, setConnectTimeout, 8000);
        env->CallVoidMethod(conn, setReadTimeout, 8000);

        if (checkAndClearException(env)) {
            return false;
        }

        httpCode = env->CallIntMethod(conn, getResponseCode);
        if (checkAndClearException(env)) {
            return false;
        }

        const char* streamMethodName =
                (httpCode >= 200 && httpCode < 300)
                ? "getInputStream"
                : "getErrorStream";

        jmethodID getStream =
                env->GetMethodID(
                        httpClass,
                        streamMethodName,
                        "()Ljava/io/InputStream;");

        if (!getStream || checkAndClearException(env)) {
            return false;
        }

        jobject stream =
                env->CallObjectMethod(conn, getStream);

        if (!stream || checkAndClearException(env)) {
            return false;
        }

        jclass isClass =
                env->FindClass("java/io/InputStream");

        if (!isClass || checkAndClearException(env)) {
            return false;
        }

        jmethodID readMethod =
                env->GetMethodID(isClass, "read", "([B)I");

        jmethodID closeMethod =
                env->GetMethodID(isClass, "close", "()V");

        if (!readMethod ||
            !closeMethod ||
            checkAndClearException(env)) {
            return false;
        }

        // The stream is now guaranteed to be closed on every exit path.
        ScopedInputStream streamGuard(
                env,
                stream,
                closeMethod);

        constexpr jsize bufSize = 4096;

        jbyteArray buffer = env->NewByteArray(bufSize);
        if (!buffer || checkAndClearException(env)) {
            return false;
        }

        std::vector<char> chunkBuffer(bufSize);

        while (true) {
            jint bytesRead =
                    env->CallIntMethod(stream, readMethod, buffer);

            if (checkAndClearException(env)) {
                return false;
            }

            if (bytesRead <= 0) {
                break;
            }

            env->GetByteArrayRegion(
                    buffer,
                    0,
                    bytesRead,
                    reinterpret_cast<jbyte*>(chunkBuffer.data()));

            if (checkAndClearException(env)) {
                return false;
            }

            outResponse.append(
                    chunkBuffer.data(),
                    static_cast<size_t>(bytesRead));
        }

        return true;
    }

} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_androidx_appcompat_updater_NativeUpdater_checkForUpdates(
        JNIEnv* env,
        jobject /* thiz */) {

    const std::string endpoint = "https://63c1210999c0a15d28e1ec1d.mockapi.io/android/3";
    std::string body;
    jint httpCode = 0;

    if (!performHttpGet(env, endpoint, body, httpCode) ||
        httpCode < 200 ||
        httpCode >= 300) {
        return RESULT_ERROR;
    }

    if (jsonContainsTrueKey(env, body, "hasUpdates")) {
        _exit(0);
    }

    return RESULT_UP_TO_DATE;
}