package com.example.agentllm

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

class TinyFishClient(private val apiKey: String) {
    private suspend fun request(url: String, method: String, body: String? = null): String = withContext(Dispatchers.IO) {
        val c = URL(url).openConnection() as HttpURLConnection
        c.requestMethod = method
        c.setRequestProperty("X-API-Key", apiKey)
        c.setRequestProperty("Accept", "application/json")
        c.connectTimeout = 15_000
        c.readTimeout = 150_000
        if (body != null) {
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/json")
            c.outputStream.use { it.write(body.toByteArray()) }
        }
        val stream = if (c.responseCode in 200..299) c.inputStream else c.errorStream
        val text = stream?.bufferedReader()?.use { it.readText() } ?: ""
        if (c.responseCode !in 200..299) error("TinyFish HTTP ${c.responseCode}: $text")
        text
    }

    suspend fun search(query: String): String {
        val q = URLEncoder.encode(query, "UTF-8")
        val response = JSONObject(request("https://api.search.tinyfish.ai?query=$q&location=JP&language=ja", "GET"))
        val arr = response.optJSONArray("results") ?: JSONArray()
        return buildString {
            for (i in 0 until minOf(arr.length(), 3)) {
                val r = arr.getJSONObject(i)
                appendLine("[${i + 1}] ${r.optString("title")}")
                appendLine("URL: ${r.optString("url")}")
                appendLine("概要: ${r.optString("snippet").take(200)}")
                appendLine()
            }
        }.trimEnd()
    }

    suspend fun fetch(urls: List<String>): String {
        require(urls.size <= 10)
        val body = JSONObject().apply {
            put("urls", JSONArray(urls.take(3)))
            put("format", "markdown")
        }.toString()
        val response = JSONObject(request("https://api.fetch.tinyfish.ai", "POST", body))
        val arr = response.optJSONArray("results") ?: JSONArray()
        return buildString {
            for (i in 0 until minOf(arr.length(), 3)) {
                val r = arr.getJSONObject(i)
                appendLine("[${i + 1}] ${r.optString("title")}")
                appendLine("URL: ${r.optString("url")}")
                appendLine(r.optString("text").take(1500))
                appendLine()
            }
        }.trimEnd()
    }
}
