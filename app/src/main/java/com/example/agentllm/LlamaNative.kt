package com.example.agentllm

object LlamaNative {
    init { System.loadLibrary("agentllm") }

    interface TokenCallback {
        fun onToken(token: ByteArray)
    }

    external fun initBackend(): Boolean
    external fun loadModel(path: String): Boolean
    external fun createContext(nCtx: Int, nThreads: Int): Boolean
    external fun resetContext()
    external fun applyChatTemplate(messagesJson: String, toolsJson: String): String
    external fun generate(prompt: String, maxTokens: Int, callback: TokenCallback? = null): String
    external fun parseToolCalls(generated: String): String
    external fun freeContext()
    external fun freeModel()
}
