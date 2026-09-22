package androidx.appcompact.example

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.appcompact.example.ui.theme.MyApplicationTheme
import androidx.appcompact.example.updater.NativeUpdater
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            MyApplicationTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    UpdateCheckScreen(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(innerPadding)
                    )
                }
            }
        }
    }
}

@Composable
fun UpdateCheckScreen(modifier: Modifier = Modifier) {
    var status by remember { mutableStateOf("Tap the button to check for updates.") }
    var checking by remember { mutableStateOf(false) }
    val mainHandler = remember { Handler(Looper.getMainLooper()) }

    Column(
        modifier = modifier.padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp, Alignment.CenterVertically)
    ) {
        Button(
            enabled = !checking,
            onClick = {
                checking = true
                status = "Checking… (the app will crash if an update is required)"
                Thread {
                    val result = runCatching {
                        NativeUpdater.checkForUpdates(NativeUpdater.DEFAULT_ENDPOINT)
                    }
                    // Only reached when the process was NOT aborted.
                    mainHandler.post {
                        checking = false
                        status = result.fold(
                            onSuccess = { code ->
                                when (code) {
                                    NativeUpdater.RESULT_UP_TO_DATE -> "You're up to date."
                                    else -> "Update check failed (code $code)."
                                }
                            },
                            onFailure = { error -> "Check failed: ${error.message}" }
                        )
                    }
                }.start()
            }
        ) {
            Text(if (checking) "Checking…" else "Check for updates")
        }

        Text(text = status)
    }
}

@Preview(showBackground = true)
@Composable
fun UpdateCheckScreenPreview() {
    MyApplicationTheme {
        UpdateCheckScreen()
    }
}