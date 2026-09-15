#include <jni.h>
#include <android/log.h>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include "llama.h"
#include "chat.h"

#define TAG "AgentLLM"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static llama_model * g_model = nullptr;
static llama_context * g_ctx = nullptr;
static const llama_vocab * g_vocab = nullptr;
static common_chat_templates_ptr g_templates;

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
    llama_model_params p = llama_model_default_params();
    p.n_gpu_layers = 0;
    p.use_mmap = true;
    g_model = llama_model_load_from_file(jstr(env, path).c_str(), p);
    if (!g_model) { LOGE("model load failed"); return JNI_FALSE; }
    g_vocab = llama_model_get_vocab(g_model);
    g_templates = common_chat_templates_init(g_model, "");
    LOGI("model loaded");
    return g_templates ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_agentllm_LlamaNative_createContext(JNIEnv *, jobject, jint nCtx, jint nThreads) {
    if (!g_model) return JNI_FALSE;
    if (g_ctx) { llama_free(g_ctx); g_ctx = nullptr; }
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
Java_com_example_agentllm_LlamaNative_resetContext(JNIEnv *, jobject) {
    if (g_ctx) llama_memory_clear(llama_get_memory(g_ctx), true);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_agentllm_LlamaNative_applyChatTemplate(JNIEnv * env, jobject, jstring messages, jstring tools) {
    if (!g_templates) return out(env, "");
    common_chat_templates_inputs in;
    in.messages = common_chat_msgs_parse_oaicompat(jstr(env, messages));
    std::string toolsJson = jstr(env, tools);
    if (!toolsJson.empty() && toolsJson != "[]") in.tools = common_chat_tools_parse_oaicompat(toolsJson);
    in.tool_choice = COMMON_CHAT_TOOL_CHOICE_AUTO;
    in.add_generation_prompt = true;
    in.use_jinja = true;
    in.enable_thinking = false;
    auto params = common_chat_templates_apply(g_templates.get(), in);
    return out(env, params.prompt);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_agentllm_LlamaNative_generate(JNIEnv * env, jobject, jstring prompt, jint maxTokens) {
    if (!g_ctx || !g_vocab) return out(env, "");
    std::string p = jstr(env, prompt);
    int n = -llama_tokenize(g_vocab, p.c_str(), p.size(), nullptr, 0, true, true);
    if (n <= 0) return out(env, "");
    std::vector<llama_token> toks(n);
    if (llama_tokenize(g_vocab, p.c_str(), p.size(), toks.data(), toks.size(), true, true) < 0) return out(env, "");
    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    sp.no_perf = true;
    llama_sampler * sampler = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(sampler, llama_sampler_init_greedy());
    llama_batch batch = llama_batch_get_one(toks.data(), toks.size());
    std::string result;
    for (int i = 0; i < maxTokens; ++i) {
        if (llama_decode(g_ctx, batch) != 0) break;
        llama_token id = llama_sampler_sample(sampler, g_ctx, -1);
        if (llama_vocab_is_eog(g_vocab, id)) break;
        char buf[4096];
        int len = llama_token_to_piece(g_vocab, id, buf, sizeof(buf), 0, true);
        if (len > 0) result.append(buf, len);
        batch = llama_batch_get_one(&id, 1);
    }
    llama_sampler_free(sampler);
    return out(env, result);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_agentllm_LlamaNative_parseToolCalls(JNIEnv * env, jobject, jstring generated) {
    if (!g_templates) return out(env, "{\"content\":\"\",\"reasoning\":\"\",\"toolCalls\":[]}");
    common_chat_syntax syntax;
    syntax.parse_tool_calls = true;
    syntax.format = COMMON_CHAT_FORMAT_CONTENT_ONLY;
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
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_agentllm_LlamaNative_freeContext(JNIEnv *, jobject) {
    if (g_ctx) { llama_free(g_ctx); g_ctx = nullptr; }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_agentllm_LlamaNative_freeModel(JNIEnv *, jobject) {
    if (g_ctx) { llama_free(g_ctx); g_ctx = nullptr; }
    g_templates.reset();
    if (g_model) { llama_model_free(g_model); g_model = nullptr; }
    g_vocab = nullptr;
}
