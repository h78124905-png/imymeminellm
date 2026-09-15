-keepclasseswithmembernames class com.example.agentllm.LlamaNative { native <methods>; }
-keep class com.example.agentllm.LlamaNative { *; }

# Tink / errorprone annotations (security-crypto 経由)
-dontwarn com.google.errorprone.annotations.**
-dontwarn javax.annotation.**
-dontwarn javax.annotation.concurrent.**
