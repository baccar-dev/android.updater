package androidx.appcompact.example.update

/**
 * Thin Kotlin wrapper around the native `updatecheck` library.
 *
 * The native layer is responsible for evaluating the update payload (the
 * `hasUpdate` flag) that was downloaded by [UpdateChecker]. The result is
 * returned to the app so it can decide how to react (for example, showing a
 * forced-update screen).
 */
object NativeUpdate {
    init {
        System.loadLibrary("updatecheck")
    }

    /**
     * Returns `true` when the given JSON response contains `"hasUpdate": true`.
     */
    fun hasUpdate(jsonResponse: String): Boolean = hasUpdateNative(jsonResponse)

    private external fun hasUpdateNative(jsonResponse: String): Boolean
}
