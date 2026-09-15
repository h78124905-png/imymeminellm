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

    fun chat(messages: List<ChatMessage>, tools: JSONArray): Pair<String, List<ToolCall>> {
        check(loaded) { "model is not loaded" }
        LlamaNative.resetContext()
        val messageJson = JSONArray().apply {
            messages.forEach { m ->
                put(JSONObject().apply {
                    put("role", m.role)
                    put("content", m.content)
                    if (m.toolCallId != null) put("tool_call_id", m.toolCallId)
                    if (m.toolCalls.isNotEmpty()) {
                        put("tool_calls", JSONArray().apply {
                            m.toolCalls.forEach { c ->
                                put(JSONObject().apply {
                                    put("id", c.id); put("type", "function")
                                    put("function", JSONObject().apply { put("name", c.name); put("arguments", c.arguments) })
                                })
                            }
                        })
                    }
                })
            }
        }
        val prompt = LlamaNative.applyChatTemplate(messageJson.toString(), tools.toString())
        val raw = LlamaNative.generate(prompt, 512)
        val parsed = JSONObject(LlamaNative.parseToolCalls(raw))
        val calls = mutableListOf<ToolCall>()
        val arr = parsed.optJSONArray("toolCalls") ?: JSONArray()
        for (i in 0 until arr.length()) {
            val o = arr.getJSONObject(i)
            calls += ToolCall(o.optString("id", "call-$i"), o.getString("name"), o.optString("arguments", "{}"))
        }
        return parsed.optString("content") to calls
    }

    fun close() { if (loaded) { LlamaNative.freeContext(); LlamaNative.freeModel(); loaded = false } }
}
