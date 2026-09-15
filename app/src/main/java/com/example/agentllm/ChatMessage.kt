package com.example.agentllm

data class ChatMessage(val role: String, val content: String, val toolCallId: String? = null, val toolCalls: List<ToolCall> = emptyList())
data class ToolCall(val id: String, val name: String, val arguments: String)
