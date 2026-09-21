#include <jni.h>
#include <string>
#include <android/log.h>

#define LOG_TAG "UpdateCheck"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

// Minimal, dependency-free check for a JSON boolean field of the form
//   "hasUpdate": true
// It tolerates arbitrary whitespace between the key, the colon and the value.
bool jsonBoolFieldIsTrue(const std::string &json, const std::string &key) {
    const std::string quotedKey = "\"" + key + "\"";
    size_t pos = json.find(quotedKey);
    if (pos == std::string::npos) {
        return false;
    }

    pos += quotedKey.size();

    // Skip whitespace up to the colon.
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                 json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    if (pos >= json.size() || json[pos] != ':') {
        return false;
    }
    pos++;

    // Skip whitespace after the colon.
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                 json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }

    return json.compare(pos, 4, "true") == 0;
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_androidx_appcompact_example_updater_NativeUpdate_hasUpdateNative(
        JNIEnv *env, jobject /* thiz */, jstring jsonResponse) {
    if (jsonResponse == nullptr) {
        return JNI_FALSE;
    }

    const char *chars = env->GetStringUTFChars(jsonResponse, nullptr);
    if (chars == nullptr) {
        return JNI_FALSE;
    }

    std::string json(chars);
    env->ReleaseStringUTFChars(jsonResponse, chars);

    const bool hasUpdate = jsonBoolFieldIsTrue(json, "hasUpdate");
    LOGI("Parsed hasUpdate=%s", hasUpdate ? "true" : "false");

    return hasUpdate ? JNI_TRUE : JNI_FALSE;
}
