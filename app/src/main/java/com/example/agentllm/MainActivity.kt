package com.example.agentllm

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) { super.onCreate(savedInstanceState); setContent { AgentApp() } }
}

@Composable
private fun AgentApp(vm: ChatViewModel = viewModel()) {
    val messages by vm.messages.collectAsState()
    val status by vm.status.collectAsState()
    val recent by vm.recent.collectAsState()
    var input by remember { mutableStateOf("") }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> if (uri != null) vm.onModelUri(uri) }
    MaterialTheme(colorScheme = darkColorScheme()) {
        Surface(Modifier.fillMaxSize()) {
            Column(Modifier.fillMaxSize().padding(12.dp)) {
                Text("AgentLLM", style = MaterialTheme.typography.headlineSmall)
                Text(status, style = MaterialTheme.typography.labelMedium)
                Row(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
                    Button(onClick = { picker.launch(arrayOf("application/octet-stream", "*/*")) }) { Text("GGUFを選択") }
                }
                if (recent.isNotEmpty()) Text("最近使ったモデル", style = MaterialTheme.typography.labelLarge)
                recent.take(3).forEach { path ->
                    OutlinedButton(onClick = { vm.loadModel(path) }, modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)) { Text(path.substringAfterLast('/')) }
                }
                LazyColumn(Modifier.weight(1f).fillMaxWidth(), contentPadding = PaddingValues(vertical = 8.dp)) {
                    items(messages) { m ->
                        Column(Modifier.fillMaxWidth().padding(vertical = 5.dp)) {
                            Text(if (m.role == "user") "あなた" else "AI", style = MaterialTheme.typography.labelSmall)
                            Text(m.content)
                        }
                    }
                }
                Row(Modifier.fillMaxWidth(), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                    OutlinedTextField(input, { input = it }, Modifier.weight(1f), placeholder = { Text("メッセージ") })
                    Spacer(Modifier.width(8.dp))
                    Button(onClick = { vm.send(input); input = "" }, enabled = input.isNotBlank()) { Text("送信") }
                }
            }
        }
    }
}
