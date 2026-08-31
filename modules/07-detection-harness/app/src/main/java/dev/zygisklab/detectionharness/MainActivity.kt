package dev.zygisklab.detectionharness

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

/**
 * The whole UI for this lab: a scrollable list of check names with their
 * outcome and evidence, plus a way to copy or share the full report as
 * text. There is nothing here for the app to do besides run its checks and
 * show what it found -- see the module README for why that is deliberate.
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // These checks are all local file reads and one short-lived child
        // process (getprop); they run in well under the time a frame takes
        // to draw, so there is no need for a background thread or a
        // loading state -- running them once, synchronously, before the
        // first Compose frame keeps this activity's one job legible.
        val results = Report.run(applicationContext)
        val reportText = Report.asText(applicationContext, results)

        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    HarnessScreen(
                        results = results,
                        reportText = reportText,
                        onCopy = { copyToClipboard(applicationContext, reportText) },
                        onShare = { shareText(reportText) },
                    )
                }
            }
        }
    }

    private fun copyToClipboard(context: Context, text: String) {
        val clipboard = context.getSystemService(ClipboardManager::class.java)
        clipboard.setPrimaryClip(ClipData.newPlainText("ZygiskLab Lab 7 report", text))
    }

    private fun shareText(text: String) {
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_TEXT, text)
        }
        startActivity(Intent.createChooser(intent, "Share detection harness report"))
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun HarnessScreen(
    results: List<CheckResult>,
    reportText: String,
    onCopy: () -> Unit,
    onShare: () -> Unit,
) {
    var copied by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(title = { Text("Detection Harness -- Lab 7") })
        },
    ) { padding ->
        Column(modifier = Modifier.fillMaxSize().padding(padding)) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(12.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(onClick = { onCopy(); copied = true }) {
                    Text(if (copied) "Copied" else "Copy report")
                }
                Button(onClick = onShare) {
                    Text("Share report")
                }
            }

            LazyColumn(
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(12.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                items(results) { result ->
                    CheckCard(result)
                }
            }
        }
    }
}

@Composable
private fun CheckCard(result: CheckResult) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(
                text = "${result.name} -- ${result.outcome.label.uppercase()}",
                style = MaterialTheme.typography.titleMedium,
            )
            Text(
                text = result.description,
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(top = 2.dp, bottom = 6.dp),
            )
            for (line in result.evidence) {
                Text(
                    text = "• $line",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }
    }
}
