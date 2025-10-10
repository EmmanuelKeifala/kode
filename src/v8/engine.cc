#include "engine_iface.h"

// Build without V8: provide stubs
#ifndef KODE_WITH_V8
namespace kode { namespace v8embed {

bool available() { return false; }
bool initialize(std::string* /*error_out*/) { return false; }
void shutdown() {}
std::string runScript(const std::string& /*code*/, std::string* error_out) {
    if (error_out) *error_out = "V8 not available (build without KODE_WITH_V8)";
    return std::string();
}

}} // namespace
#else

// Build with V8
#include <v8.h>
#include <libplatform/libplatform.h>
#include <memory>

namespace kode { namespace v8embed {

static std::unique_ptr<v8::Platform> g_platform;
static v8::Isolate* g_isolate = nullptr;
static v8::ArrayBuffer::Allocator* g_allocator = nullptr;

bool available() { return true; }

bool initialize(std::string* error_out) {
    if (g_isolate) return true;
    v8::V8::InitializeICUDefaultLocation(nullptr);
    v8::V8::InitializeExternalStartupData(nullptr);
    g_platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(g_platform.get());
    v8::V8::Initialize();

    v8::Isolate::CreateParams create_params;
    g_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    create_params.array_buffer_allocator = g_allocator;
    g_isolate = v8::Isolate::New(create_params);
    if (!g_isolate) {
        if (error_out) *error_out = "Failed to create V8 Isolate";
        return false;
    }
    return true;
}

void shutdown() {
    if (!g_isolate) return;
    g_isolate->Dispose();
    g_isolate = nullptr;
    if (g_allocator) {
        delete g_allocator;
        g_allocator = nullptr;
    }
    v8::V8::Dispose();
#if defined(V8_MAJOR_VERSION)
# if (V8_MAJOR_VERSION >= 7)
    v8::V8::DisposePlatform();
# endif
#endif
    g_platform.reset();
}

std::string runScript(const std::string& code, std::string* error_out) {
    if (!g_isolate) {
        if (error_out) *error_out = "V8 not initialized";
        return std::string();
    }
    v8::Isolate::Scope isolate_scope(g_isolate);
    v8::HandleScope handle_scope(g_isolate);
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(g_isolate);
    v8::Local<v8::Context> context = v8::Context::New(g_isolate, nullptr, global);
    if (context.IsEmpty()) {
        if (error_out) *error_out = "Failed to create V8 context";
        return std::string();
    }
    v8::Context::Scope context_scope(context);

    v8::TryCatch try_catch(g_isolate);
    v8::Local<v8::String> source;
    if (!v8::String::NewFromUtf8(g_isolate, code.c_str(), v8::NewStringType::kNormal).ToLocal(&source)) {
        if (error_out) *error_out = "Failed to create V8 source string";
        return std::string();
    }

    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(context, source).ToLocal(&script)) {
        if (error_out) {
            v8::String::Utf8Value err(g_isolate, try_catch.Exception());
            *error_out = err.length() ? *err : "Script compile error";
        }
        return std::string();
    }

    v8::Local<v8::Value> result;
    if (!script->Run(context).ToLocal(&result)) {
        if (error_out) {
            v8::String::Utf8Value err(g_isolate, try_catch.Exception());
            *error_out = err.length() ? *err : "Script run error";
        }
        return std::string();
    }

    v8::String::Utf8Value utf8(g_isolate, result);
    if (*utf8) return std::string(*utf8, utf8.length());
    return std::string();
}

}} // namespace
#endif
