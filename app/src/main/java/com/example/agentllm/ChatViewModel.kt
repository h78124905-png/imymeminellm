package com.example.agentllm

import android.app.Application
import android.content.Intent
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class ChatViewModel(app: Application) : AndroidViewModel(app) {
    private val engine = LlamaEngine()
    private var agent: AgentLoop? = null
    private var isNewConversation = true
    private val keyStore = SecureKeyStore(app)
    private val prefs = app.getSharedPreferences("models", 0)
    private val _messages = MutableStateFlow<List<ChatMessage>>(emptyList())
    val messages: StateFlow<List<ChatMessage>> = _messages
    private val _status = MutableStateFlow("モデル未選択")
    val status: StateFlow<String> = _status
    private val _recent = MutableStateFlow(loadRecent())
    val recent: StateFlow<List<String>> = _recent
    private val _streamingText = MutableStateFlow("")
    val streamingText: StateFlow<String> = _streamingText
    private val _reasoningText = MutableStateFlow("")
    val reasoningText: StateFlow<String> = _reasoningText
    private val _stage = MutableStateFlow("")
    val stage: StateFlow<String> = _stage
    private val _loading = MutableStateFlow(false)
    val loading: StateFlow<Boolean> = _loading

    private fun loadRecent() = prefs.getStringSet("recent", emptySet())?.toList() ?: emptyList()
    private fun saveRecent(path: String) {
        val list = (listOf(path) + _recent.value.filter { it != path }).take(5)
        _recent.value = list
        prefs.edit().putStringSet("recent", list.toSet()).apply()
    }

    fun loadModel(path: String) = viewModelScope.launch {
        _status.value = "モデル読み込み中…"
        runCatching {
            engine.load(path)
            agent = AgentLoop(engine, TinyFishClient(keyStore.getTinyFishKey()))
            saveRecent(path)
        }.onSuccess { _status.value = "準備完了" }
         .onFailure { _status.value = "読み込み失敗: ${it.message}" }
    }

    fun onModelUri(uri: android.net.Uri) = viewModelScope.launch(Dispatchers.IO) {
        val resolver = getApplication<Application>().contentResolver
        runCatching { resolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        _status.value = "モデルをコピー中…"
        val name = resolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else "model.gguf"
        } ?: "model.gguf"
        val file = File(getApplication<Application>().getExternalFilesDir("models"), name)
        resolver.openInputStream(uri)!!.use { input -> file.outputStream().use { output -> input.copyTo(output) } }
        withContext(Dispatchers.Main) { loadModel(file.absolutePath) }
    }

    fun send(text: String) = viewModelScope.launch {
        if (text.isBlank() || agent == null || _loading.value) return@launch
        val before = _messages.value + ChatMessage("user", text)
        _messages.value = before
        _loading.value = true
        _streamingText.value = ""
        _reasoningText.value = ""
        _stage.value = "準備しています…"

        runCatching {
            if (isNewConversation) {
                engine.reset()
                isNewConversation = false
            }
            agent!!.run(
                userText = text,
                onToken = { token -> _streamingText.value += token },
                onStage = { stage -> _stage.value = stage }
            )
        }.onSuccess { answer ->
            _messages.value = before + answer
            _reasoningText.value = answer.reasoning
            _streamingText.value = ""
        }.onFailure { e ->
            _messages.value = before + ChatMessage("assistant", "エラー: ${e.message}")
            _streamingText.value = ""
            _reasoningText.value = ""
        }
        _loading.value = false
        _stage.value = ""
        _status.value = "準備完了"
    }

    fun startNewConversation() {
        isNewConversation = true
        _messages.value = emptyList()
        _streamingText.value = ""
        _reasoningText.value = ""
        _stage.value = ""
    }

    override fun onCleared() { engine.close(); super.onCleared() }
}
