#include <jni.h>
#include <android/log.h>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include "llama.h"
#include "common.h"
#include "chat.h"

#define TAG "AgentLLM"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static llama_model * g_model = nullptr;
static llama_context * g_ctx = nullptr;
static const llama_vocab * g_vocab = nullptr;
static common_chat_templates_ptr g_tmpls;
static std::vector<llama_token> g_cached_tokens;

static std::string jstr(JNIEnv * env, jstring s) {
    if (!s) return {};
    const char * p = env->GetStringUTFChars(s, nullptr);
    std::string out = p ? p : "";
    if (p) env->ReleaseStringUTFChars(s, p);
    return out;
}

static std::string json_escape(const std::string & s) {
    std::ostringstream o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (c < 0x20) o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c << std::dec;
                else o << (char)c;
        }
    }
    return o.str();
}

static jstring out(JNIEnv * env, const std::string & s) { return env->NewStringUTF(s.c_str()); }

static size_t utf8_complete_prefix(const std::string & bytes) {
    size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        size_t need = 0;
        uint32_t cp = 0;
        if (c <= 0x7f) { need = 1; cp = c; }
        else if (c >= 0xc2 && c <= 0xdf) { need = 2; cp = c & 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { need = 3; cp = c & 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { need = 4; cp = c & 0x07; }
        else { ++i; continue; }
        if (i + need > bytes.size()) break;
        bool valid = true;
        for (size_t j = 1; j < need; ++j) {
            const unsigned char cc = static_cast<unsigned char>(bytes[i + j]);
            if ((cc & 0xc0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (cc & 0x3f);
        }
        if (!valid || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
            (need == 3 && cp < 0x800) || (need == 4 && cp < 0x10000)) { ++i; continue; }
        i += need;
    }
    return i;
}

static void emit_utf8(JNIEnv * env, jobject callback, jmethodID onToken, const std::string & bytes) {
    if (!callback || !onToken || bytes.empty()) return;
    jbyteArray arr = env->NewByteArray(static_cast<jsize>(bytes.size()));
    if (!arr) return;
    env->SetByteArrayRegion(arr, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte *>(bytes.data()));
    env->CallVoidMethod(callback, onToken, arr);
    env->DeleteLocalRef(arr);
    if (env->ExceptionCheck()) {
        LOGE("Token callback threw an exception");
        env->ExceptionClear();
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_agentllm_LlamaNative_initBackend(JNIEnv *, jobject) {
    ggml_backend_load_all();
    LOGI("CPU backend initialized");
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_agentllm_LlamaNative_loadModel(JNIEnv * env, jobject, jstring path) {
    if (g_ctx) { llama_free(g_ctx); g_ctx = nullptr; }
    if (g_model) { llama_model_free(g_model); g_model = nullptr; }
    g_tmpls.reset();
    g_cached_tokens.clear();
    g_vocab = nullptr;
    const std::string model_path = jstr(env, path);
    llama_model_params p = llama_model_default_params();
    p.n_gpu_layers = 0;
    p.use_mmap = false;
    g_model = llama_model_load_from_file(model_path.c_str(), p);
    if (!g_model) { LOGE("model load failed"); return JNI_FALSE; }
    g_vocab = llama_model_get_vocab(g_model);
    g_tmpls = common_chat_templates_init(g_model, "");
    if (!g_tmpls) {
        llama_model_free(g_model); g_model = nullptr; g_vocab = nullptr; return JNI_FALSE;
    }
    LOGI("model loaded: %s", model_path.c_str());
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_agentllm_LlamaNative_createContext(JNIEnv *, jobject, jint nCtx, jint nThreads) {
    if (!g_model) return JNI_FALSE;
    if (g_ctx) { llama_free(g_ctx); g_ctx = nullptr; }
    g_cached_tokens.clear();
    llama_context_params p = llama_context_default_params();
    p.n_ctx = (uint32_t)nCtx;
    p.n_batch = 512;
    p.n_ubatch = 512;
    p.n_threads = nThreads;
    p.n_threads_batch = nThreads;
    p.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;
    g_ctx = llama_init_from_model(g_model, p);
    if (!g_ctx) return JNI_FALSE;
    LOGI("context created n_ctx=%d threads=%d", nCtx, nThreads);
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_agentllm_LlamaNative_setThreads(JNIEnv *, jobject, jlong ctx, jint nThreads, jint nThreadsBatch) {
    llama_context * target = ctx != 0 ? reinterpret_cast<llama_context *>(ctx) : g_ctx;
    if (target) llama_set_n_threads(target, nThreads, nThreadsBatch);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_agentllm_LlamaNative_resetContext(JNIEnv *, jobject) {
    if (g_ctx) llama_memory_clear(llama_get_memory(g_ctx), true);
    g_cached_tokens.clear();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_agentllm_LlamaNative_applyChatTemplate(JNIEnv * env, jobject, jstring messages, jstring tools) {
    if (!g_tmpls) return out(env, "");
    common_chat_templates_inputs in;
    in.messages = common_chat_msgs_parse_oaicompat(jstr(env, messages));
    const std::string toolsJson = jstr(env, tools);
    if (!toolsJson.empty() && toolsJson != "[]") in.tools = common_chat_tools_parse_oaicompat(toolsJson);
    in.tool_choice = COMMON_CHAT_TOOL_CHOICE_AUTO;
    in.add_generation_prompt = true;
    in.use_jinja = true;
    in.enable_thinking = false;
    auto params = common_chat_templates_apply(g_tmpls.get(), in);
    LOGI("Chat template applied, prompt length: %zu", params.prompt.size());
    return out(env, params.prompt);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_agentllm_LlamaNative_generate(JNIEnv * env, jobject, jstring prompt, jint maxTokens, jobject callback, jobject stageCallback) {
    if (!g_ctx || !g_vocab) return out(env, "");

    jmethodID onStage = nullptr;
    if (stageCallback) {
        jclass cls = env->GetObjectClass(stageCallback);
        if (cls) {
            onStage = env->GetMethodID(cls, "onStage", "(Ljava/lang/String;)V");
            env->DeleteLocalRef(cls);
            if (env->ExceptionCheck()) { env->ExceptionClear(); onStage = nullptr; }
        }
    }
    auto notifyStage = [&](const char * s) {
        if (!stageCallback || !onStage) return;
        jstring js = env->NewStringUTF(s);
        if (!js) return;
        env->CallVoidMethod(stageCallback, onStage, js);
        env->DeleteLocalRef(js);
        if (env->ExceptionCheck()) {
            LOGE("Stage callback threw an exception");
            env->ExceptionClear();
        }
    };

    notifyStage("reading");
    const std::string p = jstr(env, prompt);
    int n = -llama_tokenize(g_vocab, p.c_str(), p.size(), nullptr, 0, true, true);
    if (n <= 0) { notifyStage(""); return out(env, ""); }
    std::vector<llama_token> toks(n);
    if (llama_tokenize(g_vocab, p.c_str(), p.size(), toks.data(), toks.size(), true, true) < 0) {
        notifyStage(""); return out(env, "");
    }

    size_t n_match = 0;
    const size_t common = std::min(g_cached_tokens.size(), toks.size());
    while (n_match < common && g_cached_tokens[n_match] == toks[n_match]) ++n_match;

    if (n_match < g_cached_tokens.size()) {
        llama_kv_cache_seq_rm(g_ctx, 0, (int32_t)n_match, -1);
    }

    size_t decode_from = n_match;
    if (decode_from == toks.size() && !toks.empty()) {
        decode_from = toks.size() - 1;
        llama_kv_cache_seq_rm(g_ctx, 0, (int32_t)decode_from, -1);
    }

    g_cached_tokens = toks;

    jmethodID onToken = nullptr;
    if (callback) {
        jclass cls = env->GetObjectClass(callback);
        if (cls) {
            onToken = env->GetMethodID(cls, "onToken", "([B)V");
            env->DeleteLocalRef(cls);
            if (env->ExceptionCheck()) { env->ExceptionClear(); onToken = nullptr; }
        }
    }

    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    sp.no_perf = true;
    llama_sampler * sampler = llama_sampler_chain_init(sp);
    if (!sampler) { notifyStage(""); return out(env, ""); }
    llama_sampler_chain_add(sampler, llama_sampler_init_greedy());
    llama_batch batch = llama_batch_get_one(toks.data() + decode_from, toks.size() - decode_from);
    std::string result;
    std::string utf8_pending;

    if (llama_decode(g_ctx, batch) != 0) {
        llama_sampler_free(sampler);
        notifyStage("");
        return out(env, "");
    }
    notifyStage("writing");
    llama_set_n_threads(g_ctx, 3, 3);

    for (int i = 0; i < maxTokens; ++i) {
        llama_token id = llama_sampler_sample(sampler, g_ctx, -1);
        llama_sampler_accept(sampler, id);
        if (llama_vocab_is_eog(g_vocab, id)) break;

        char buf[4096];
        int len = llama_token_to_piece(g_vocab, id, buf, sizeof(buf), 0, true);
        if (len < 0) {
            std::vector<char> big(static_cast<size_t>(-len));
            len = llama_token_to_piece(g_vocab, id, big.data(), big.size(), 0, true);
            if (len > 0) {
                const std::string piece(big.data(), static_cast<size_t>(len));
                result += piece;
                if (onToken) {
                    utf8_pending += piece;
                    const size_t complete = utf8_complete_prefix(utf8_pending);
                    if (complete > 0) {
                        emit_utf8(env, callback, onToken, utf8_pending.substr(0, complete));
                        utf8_pending.erase(0, complete);
                    }
                }
            }
        } else if (len > 0) {
            const std::string piece(buf, static_cast<size_t>(len));
            result += piece;
            if (onToken) {
                utf8_pending += piece;
                const size_t complete = utf8_complete_prefix(utf8_pending);
                if (complete > 0) {
                    emit_utf8(env, callback, onToken, utf8_pending.substr(0, complete));
                    utf8_pending.erase(0, complete);
                }
            }
        }
        batch = llama_batch_get_one(&id, 1);
        if (llama_decode(g_ctx, batch) != 0) break;
    }

    if (onToken && !utf8_pending.empty()) emit_utf8(env, callback, onToken, utf8_pending);
    llama_sampler_free(sampler);
    notifyStage("");
    return out(env, result);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_agentllm_LlamaNative_parseToolCalls(JNIEnv * env, jobject, jstring generated) {
    if (!g_tmpls) return out(env, "{\"content\":\"\",\"reasoning\":\"\",\"toolCalls\":[]}");
    common_chat_syntax syntax;
    syntax.parse_tool_calls = true;
    syntax.format = COMMON_CHAT_FORMAT_CONTENT_ONLY;
    try {
        auto msg = common_chat_parse(jstr(env, generated), false, syntax);
        std::ostringstream o;
        o << "{\"content\":\"" << json_escape(msg.content) << "\",\"reasoning\":\"" << json_escape(msg.reasoning_content) << "\",\"toolCalls\":[";
        for (size_t i = 0; i < msg.tool_calls.size(); ++i) {
            if (i) o << ',';
            const auto & c = msg.tool_calls[i];
            o << "{\"id\":\"" << json_escape(c.id) << "\",\"name\":\"" << json_escape(c.name) << "\",\"arguments\":" << (c.arguments.empty() ? "{}" : c.arguments) << '}';
        }
        o << "]}";
        return out(env, o.str());
    } catch (const std::exception & e) {
        LOGE("tool call parse error: %s", e.what());
        return out(env, "{\"content\":\"\",\"reasoning\":\"\",\"toolCalls\":[]}");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_agentllm_LlamaNative_freeContext(JNIEnv *, jobject) {
    if (g_ctx) { llama_free(g_ctx); g_ctx = nullptr; }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_agentllm_LlamaNative_freeModel(JNIEnv *, jobject) {
    if (g_ctx) { llama_free(g_ctx); g_ctx = nullptr; }
    g_tmpls.reset();
    if (g_model) { llama_model_free(g_model); g_model = nullptr; }
    g_vocab = nullptr;
    g_cached_tokens.clear();
}
