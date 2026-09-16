package com.example.agentllm

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

class LlamaEngine {
    private var loaded = false

    suspend fun load(path: String): Unit = withContext(Dispatchers.Default) {
        check(LlamaNative.initBackend()) { "CPU backend initialization failed" }
        check(LlamaNative.loadModel(path)) { "GGUF model load failed" }
        val threads = minOf(Runtime.getRuntime().availableProcessors(), 6)
        check(LlamaNative.createContext(4096, threads)) { "llama context creation failed" }
        loaded = true
    }

    suspend fun reset() = withContext(Dispatchers.Default) {
        LlamaNative.resetContext()
    }

    suspend fun chat(
        messages: List<ChatMessage>,
        tools: JSONArray,
        maxTokens: Int = 512,
        onToken: (String) -> Unit = {},
        onStage: (String) -> Unit = {}
    ): ChatMessage = withContext(Dispatchers.Default) {
        check(loaded) { "model is not loaded" }

        val messageJson = JSONArray().apply {
            messages.forEach { m ->
                put(JSONObject().apply {
                    put("role", m.role)
                    put("content", m.content)
                    if (m.reasoning.isNotEmpty()) put("reasoning_content", m.reasoning)
                    if (m.toolCallId != null) put("tool_call_id", m.toolCallId)
                    if (m.toolCalls.isNotEmpty()) {
                        put("tool_calls", JSONArray().apply {
                            m.toolCalls.forEach { c ->
                                put(JSONObject().apply {
                                    put("id", c.id)
                                    put("type", "function")
                                    put("function", JSONObject().apply {
                                        put("name", c.name)
                                        put("arguments", c.arguments)
                                    })
                                })
                            }
                        })
                    }
                })
            }
        }

        val prompt = LlamaNative.applyChatTemplate(messageJson.toString(), tools.toString())
        val tokenCb = object : LlamaNative.TokenCallback {
            override fun onToken(token: ByteArray) {
                onToken(String(token, Charsets.UTF_8))
            }
        }
        val stageCb = object : LlamaNative.StageCallback {
            override fun onStage(stage: String) {
                onStage(stage)
            }
        }

        LlamaNative.setThreads(0L, 8, 8)
        val raw = LlamaNative.generate(prompt, maxTokens, tokenCb, stageCb)
        val parsed = JSONObject(LlamaNative.parseToolCalls(raw))
        val calls = mutableListOf<ToolCall>()
        val arr = parsed.optJSONArray("toolCalls") ?: parsed.optJSONArray("tool_calls") ?: JSONArray()
        for (i in 0 until arr.length()) {
            val o = arr.getJSONObject(i)
            val fn = o.optJSONObject("function")
            if (fn != null) {
                calls += ToolCall(
                    id = o.optString("id", "call-$i"),
                    name = fn.getString("name"),
                    arguments = fn.optString("arguments", "{}")
                )
            } else {
                calls += ToolCall(
                    id = o.optString("id", "call-$i"),
                    name = o.getString("name"),
                    arguments = o.optString("arguments", "{}")
                )
            }
        }

        ChatMessage(
            role = "assistant",
            content = parsed.optString("content", ""),
            reasoning = parsed.optString("reasoning", ""),
            toolCalls = calls
        )
    }

    fun close() {
        if (loaded) {
            LlamaNative.freeContext()
            LlamaNative.freeModel()
            loaded = false
        }
    }
}
