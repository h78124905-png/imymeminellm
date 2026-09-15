package com.example.agentllm

import android.content.Context
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey

class SecureKeyStore(context: Context) {
    private val prefs = EncryptedSharedPreferences.create(
        context, "agentllm_secrets", MasterKey.Builder(context).setKeyScheme(MasterKey.KeyScheme.AES256_GCM).build(),
        EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
        EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
    )
    fun getTinyFishKey(): String = prefs.getString("tinyfish_api_key", BuildConfig.TINYFISH_API_KEY) ?: ""
    fun setTinyFishKey(value: String) { prefs.edit().putString("tinyfish_api_key", value).apply() }
}
