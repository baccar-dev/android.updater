package androidx.appcompat.updater

object NativeUpdater {

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
     */
    external fun checkForUpdates(): Int
}
