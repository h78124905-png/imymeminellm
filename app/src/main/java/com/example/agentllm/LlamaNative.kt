package com.example.agentllm

object LlamaNative {
    init { System.loadLibrary("agentllm") }

    interface TokenCallback {
        fun onToken(token: ByteArray)
    }

    interface StageCallback {
        fun onStage(stage: String)
    }

    external fun initBackend(): Boolean
    external fun loadModel(path: String): Boolean
    external fun createContext(nCtx: Int, nThreads: Int): Boolean
    external fun setThreads(ctx: Long, nThreads: Int, nThreadsBatch: Int)
    external fun resetContext()
    external fun applyChatTemplate(messagesJson: String, toolsJson: String): String
    external fun generate(
        prompt: String,
        maxTokens: Int,
        tokenCallback: TokenCallback? = null,
        stageCallback: StageCallback? = null
    ): String
    external fun parseToolCalls(generated: String): String
    external fun freeContext()
    external fun freeModel()
}
