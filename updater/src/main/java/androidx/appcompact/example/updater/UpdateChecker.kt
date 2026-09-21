package androidx.appcompact.example.updater

import android.os.Handler
import android.os.Looper
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

data class UpdateCheckResult(
    val hasUpdate: Boolean,
    val responseCode: Int
)

/**
 * Downloads the update descriptor and reports whether an update is available.
 *
 * The HTTPS request is performed off the main thread and the raw payload is
 * handed to the native `updatecheck` library (see [NativeUpdate]) to evaluate
 * the `hasUpdate` flag. The result is delivered back on the main thread so the
 * app can respond appropriately.
 */
object UpdateChecker {
    const val DEFAULT_ENDPOINT =
        "https://63c1210999c0a15d28e1ec1d.mockapi.io/android/3"

    private const val CONNECT_TIMEOUT_MS = 10_000
    private const val READ_TIMEOUT_MS = 10_000

    private val networkExecutor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    fun check(
        endpoint: String = DEFAULT_ENDPOINT,
        onComplete: (Result<UpdateCheckResult>) -> Unit
    ) {
        require(endpoint.startsWith("https://")) {
            "The update endpoint must use HTTPS"
        }

        networkExecutor.execute {
            val result = runCatching {
                val connection = (URL(endpoint).openConnection() as HttpURLConnection).apply {
                    requestMethod = "GET"
                    connectTimeout = CONNECT_TIMEOUT_MS
                    readTimeout = READ_TIMEOUT_MS
                    instanceFollowRedirects = true
                }

                try {
                    val responseCode = connection.responseCode
                    if (responseCode !in 200..299) {
                        error("Update endpoint returned HTTP $responseCode")
                    }

                    val body = connection.inputStream.bufferedReader().use { it.readText() }
                    val hasUpdate = NativeUpdate.hasUpdate(body)
                    UpdateCheckResult(hasUpdate = hasUpdate, responseCode = responseCode)
                } finally {
                    connection.disconnect()
                }
            }

            mainHandler.post { onComplete(result) }
        }
    }
}
