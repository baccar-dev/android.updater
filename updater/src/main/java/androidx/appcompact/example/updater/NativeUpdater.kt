package androidx.appcompact.example.updater

/**
 * Native update enforcer.
 *
 * The entire update flow lives in the native `updatecheck` library:
 *  - the HTTPS request is performed natively (via JNI up-calls into
 *    `HttpURLConnection`, so TLS works without any extra native dependency), and
 *  - the process is terminated with `abort()` when the server reports
 *    `"hasUpdates": true`.
 *
 * This Kotlin object only loads the library and declares the JNI entry point.
 */
object NativeUpdater {

    const val DEFAULT_ENDPOINT =
        "https://63c1210999c0a15d28e1ec1d.mockapi.io/android/3"

    /** Status returned when the check completed and no update is required. */
    const val RESULT_UP_TO_DATE = 0

    /** Status returned when the check could not be completed. */
    const val RESULT_ERROR = -1

    init {
        System.loadLibrary("updatecheck")
    }

    /**
     * Runs the native update check.
     *
     * If the endpoint reports `hasUpdates = true`, the native layer calls
     * `abort()` and this method never returns (the app crashes). Otherwise it
     * returns [RESULT_UP_TO_DATE] or [RESULT_ERROR].
     *
     * Must be called off the main thread (it performs network I/O).
     */
    external fun checkForUpdates(endpoint: String): Int
}
