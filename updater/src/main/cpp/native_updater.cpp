#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <unistd.h>
#include <random>

static constexpr jint RESULT_UP_TO_DATE = 0;
static constexpr jint RESULT_ERROR = -1;
static constexpr jint RESULT_HAS_UPDATES = 1;

namespace {
    constexpr jint CONNECT_TIMEOUT_MS = 8000;
    constexpr jint READ_TIMEOUT_MS = 8000;
    constexpr jsize BUFFER_SIZE = 4096;

// Protect against a broken/malicious server returning enormous data.
    constexpr size_t MAX_RESPONSE_SIZE = 1024 * 1024; // 1 MiB


    class ScopedLocalFrame {
    public:
        explicit ScopedLocalFrame(JNIEnv *env, jint capacity = 16)
                : env_(env),
                  pushed_(env != nullptr && env->PushLocalFrame(capacity) == JNI_OK) {}

        ~ScopedLocalFrame() {
            if (pushed_) {
                env_->PopLocalFrame(nullptr);
            }
        }

        bool valid() const {
            return pushed_;
        }

        ScopedLocalFrame(const ScopedLocalFrame &) = delete;

        ScopedLocalFrame &operator=(const ScopedLocalFrame &) = delete;

    private:
        JNIEnv *env_;
        bool pushed_;
    };


    class ScopedUtfChars {
    public:
        ScopedUtfChars(JNIEnv *env, jstring string)
                : env_(env),
                  string_(string),
                  chars_(nullptr) {

            if (env_ && string_) {
                chars_ = env_->GetStringUTFChars(string_, nullptr);
            }
        }

        ~ScopedUtfChars() {
            if (env_ && string_ && chars_) {
                env_->ReleaseStringUTFChars(string_, chars_);
            }
        }

        const char *get() const {
            return chars_;
        }

        bool valid() const {
            return chars_ != nullptr;
        }

        ScopedUtfChars(const ScopedUtfChars &) = delete;

        ScopedUtfChars &operator=(const ScopedUtfChars &) = delete;

    private:
        JNIEnv *env_;
        jstring string_;
        const char *chars_;
    };


    class ScopedHttpDisconnect {
    public:
        ScopedHttpDisconnect(
                JNIEnv *env,
                jobject connection,
                jmethodID disconnectMethod)
                : env_(env),
                  connection_(connection),
                  disconnectMethod_(disconnectMethod) {}

        ~ScopedHttpDisconnect() {
            if (!env_ || !connection_ || !disconnectMethod_) {
                return;
            }

            env_->CallVoidMethod(
                    connection_,
                    disconnectMethod_);

            // A cleanup operation should never leave a pending exception.
            if (env_->ExceptionCheck()) {
                env_->ExceptionClear();
            }
        }

        ScopedHttpDisconnect(const ScopedHttpDisconnect &) = delete;

        ScopedHttpDisconnect &operator=(const ScopedHttpDisconnect &) = delete;

    private:
        JNIEnv *env_;
        jobject connection_;
        jmethodID disconnectMethod_;
    };


    class ScopedInputStream {
    public:
        ScopedInputStream(
                JNIEnv *env,
                jobject stream,
                jmethodID closeMethod)
                : env_(env),
                  stream_(stream),
                  closeMethod_(closeMethod) {}

        ~ScopedInputStream() {
            if (!env_ || !stream_ || !closeMethod_) {
                return;
            }

            env_->CallVoidMethod(
                    stream_,
                    closeMethod_);

            if (env_->ExceptionCheck()) {
                env_->ExceptionClear();
            }
        }

        ScopedInputStream(const ScopedInputStream &) = delete;

        ScopedInputStream &operator=(const ScopedInputStream &) = delete;

    private:
        JNIEnv *env_;
        jobject stream_;
        jmethodID closeMethod_;
    };


    bool clearException(JNIEnv *env) {
        if (!env || !env->ExceptionCheck()) {
            return false;
        }

        // In production, log this exception before clearing it.
        env->ExceptionDescribe();
        env->ExceptionClear();

        return true;
    }


    bool getHttpConnectionMethods(
            JNIEnv *env,
            jclass httpClass,
            jmethodID &disconnectMethod,
            jmethodID &setRequestMethod,
            jmethodID &setConnectTimeout,
            jmethodID &setReadTimeout,
            jmethodID &getResponseCode) {

        disconnectMethod =
                env->GetMethodID(httpClass, "disconnect", "()V");

        setRequestMethod =
                env->GetMethodID(
                        httpClass,
                        "setRequestMethod",
                        "(Ljava/lang/String;)V");

        setConnectTimeout =
                env->GetMethodID(
                        httpClass,
                        "setConnectTimeout",
                        "(I)V");

        setReadTimeout =
                env->GetMethodID(
                        httpClass,
                        "setReadTimeout",
                        "(I)V");

        getResponseCode =
                env->GetMethodID(
                        httpClass,
                        "getResponseCode",
                        "()I");

        if (clearException(env)) {
            return false;
        }

        return disconnectMethod &&
               setRequestMethod &&
               setConnectTimeout &&
               setReadTimeout &&
               getResponseCode;
    }


    bool
    performHttpGet(JNIEnv *env, const std::string &url, std::string &response, jint &httpCode) {

        response.clear();
        httpCode = 0;

        ScopedLocalFrame frame(env, 32);
        if (!frame.valid()) {
            return false;
        }

        // ---------------------------------------------------------------------
        // URL
        // ---------------------------------------------------------------------

        jclass urlClass = env->FindClass("java/net/URL");

        if (!urlClass || clearException(env)) {
            return false;
        }

        jmethodID urlConstructor = env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V");

        jmethodID openConnection = env->GetMethodID(urlClass, "openConnection",
                                                    "()Ljava/net/URLConnection;");

        if (!urlConstructor ||
            !openConnection ||
            clearException(env)) {
            return false;
        }

        jstring jUrl = env->NewStringUTF(url.c_str());

        if (!jUrl || clearException(env)) {
            return false;
        }

        jobject urlObject = env->NewObject(urlClass, urlConstructor, jUrl);

        if (!urlObject || clearException(env)) {
            return false;
        }

        // ---------------------------------------------------------------------
        // Connection
        // ---------------------------------------------------------------------

        jobject connection = env->CallObjectMethod(urlObject, openConnection);

        if (!connection || clearException(env)) {
            return false;
        }

        jclass httpClass = env->FindClass("java/net/HttpURLConnection");

        if (!httpClass || clearException(env)) {
            return false;
        }

        // Make sure this really is an HttpURLConnection.
        if (!env->IsInstanceOf(connection, httpClass)) {
            return false;
        }

        jmethodID disconnectMethod = nullptr;
        jmethodID setRequestMethod = nullptr;
        jmethodID setConnectTimeout = nullptr;
        jmethodID setReadTimeout = nullptr;
        jmethodID getResponseCode = nullptr;

        if (!getHttpConnectionMethods(
                env,
                httpClass,
                disconnectMethod,
                setRequestMethod,
                setConnectTimeout,
                setReadTimeout,
                getResponseCode)) {
            return false;
        }

        ScopedHttpDisconnect disconnect(env, connection, disconnectMethod);

        // ---------------------------------------------------------------------
        // Configure request
        // ---------------------------------------------------------------------

        jstring getString = env->NewStringUTF("GET");

        if (!getString || clearException(env)) {
            return false;
        }

        env->CallVoidMethod(connection, setRequestMethod, getString);

        if (clearException(env)) {
            return false;
        }

        env->CallVoidMethod(connection, setConnectTimeout, CONNECT_TIMEOUT_MS);

        if (clearException(env)) {
            return false;
        }

        env->CallVoidMethod(connection, setReadTimeout, READ_TIMEOUT_MS);

        if (clearException(env)) {
            return false;
        }

        // ---------------------------------------------------------------------
        // Response code
        // ---------------------------------------------------------------------

        httpCode = env->CallIntMethod(connection, getResponseCode);

        if (clearException(env)) {
            return false;
        }

        if (httpCode < 200 || httpCode >= 300) {
            // We don't need to download an error response.
            return false;
        }

        // ---------------------------------------------------------------------
        // Input stream
        // ---------------------------------------------------------------------

        jmethodID getInputStream = env->GetMethodID(httpClass, "getInputStream",
                                                    "()Ljava/io/InputStream;");

        if (!getInputStream || clearException(env)) {
            return false;
        }

        jobject stream = env->CallObjectMethod(connection, getInputStream);

        if (!stream || clearException(env)) {
            return false;
        }

        jclass inputStreamClass = env->FindClass("java/io/InputStream");

        if (!inputStreamClass || clearException(env)) {
            return false;
        }

        jmethodID readMethod = env->GetMethodID(inputStreamClass, "read", "([B)I");

        jmethodID closeMethod = env->GetMethodID(inputStreamClass, "close", "()V");

        if (!readMethod ||
            !closeMethod ||
            clearException(env)) {
            return false;
        }

        ScopedInputStream streamGuard(env, stream, closeMethod);

        // ---------------------------------------------------------------------
        // Read response
        // ---------------------------------------------------------------------

        jbyteArray buffer = env->NewByteArray(BUFFER_SIZE);

        if (!buffer || clearException(env)) {
            return false;
        }

        std::vector<char> chunk(BUFFER_SIZE);

        while (true) {
            jint bytesRead = env->CallIntMethod(stream, readMethod, buffer);

            if (clearException(env)) {
                return false;
            }

            if (bytesRead == -1) {
                break;
            }

            if (bytesRead <= 0) {
                // Defensive handling; InputStream normally returns either
                // a positive count or -1 for a non-empty buffer.
                return false;
            }

            if (response.size() > MAX_RESPONSE_SIZE - static_cast<size_t>(bytesRead)) {
                return false;
            }

            env->GetByteArrayRegion(
                    buffer,
                    0,
                    bytesRead,
                    reinterpret_cast<jbyte *>(chunk.data()));

            if (clearException(env)) {
                return false;
            }

            response.append(chunk.data(), static_cast<size_t>(bytesRead));
        }

        return true;
    }


    bool jsonContainsTrueKey(JNIEnv *env, const std::string &json, const char *key) {

        ScopedLocalFrame frame(env, 16);

        if (!frame.valid()) {
            return false;
        }

        jclass jsonClass = env->FindClass("org/json/JSONObject");

        if (!jsonClass || clearException(env)) {
            return false;
        }

        jmethodID constructor = env->GetMethodID(jsonClass, "<init>", "(Ljava/lang/String;)V");

        jmethodID optBoolean = env->GetMethodID(jsonClass, "optBoolean", "(Ljava/lang/String;Z)Z");

        if (!constructor ||
            !optBoolean ||
            clearException(env)) {
            return false;
        }

        jstring jsonString = env->NewStringUTF(json.c_str());

        if (!jsonString || clearException(env)) {
            return false;
        }

        jobject jsonObject = env->NewObject(jsonClass, constructor, jsonString);

        if (!jsonObject || clearException(env)) {
            return false;
        }

        jstring keyString = env->NewStringUTF(key);
        env->NewStringUTF(key);

        if (!keyString || clearException(env)) {
            return false;
        }

        jboolean result = env->CallBooleanMethod(jsonObject, optBoolean, keyString, JNI_FALSE);

        if (clearException(env)) {
            return false;
        }

        return result == JNI_TRUE;
    }

} // namespace


extern "C"
JNIEXPORT jint JNICALL
Java_androidx_appcompat_updater_NativeUpdater_checkForUpdates(JNIEnv *env, jobject /* thiz */) {
    try {
        if (!env) {
            return RESULT_ERROR;
        }
//only on release builds
#ifndef NDEBUG
        // In debug builds, we can simulate an update being available
        return RESULT_UP_TO_DATE;
#endif

        //make it random to test the update flow
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<int> distribution(0, 100);

        const int randomNumber = distribution(generator);
        if (randomNumber % 4 != 0) {
            return RESULT_UP_TO_DATE;
        }

        // Prefer configuration supplied by Java/Kotlin or a trusted
        // application configuration rather than hard-coding this.
        const std::string endpoint = "https://63c1210999c0a15d28e1ec1d.mockapi.io/android/3";

        std::string response;
        jint httpCode = 0;

        if (!performHttpGet(env, endpoint, response, httpCode)) {
            return RESULT_ERROR;
        }

        if (jsonContainsTrueKey(env, response, "hasUpdates")) {
            //return RESULT_HAS_UPDATES;
            _exit(0);
        }

        return RESULT_UP_TO_DATE;

    } catch (...) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return RESULT_ERROR;
    }
}