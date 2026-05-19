#include "engine_iface.h"

// Build without V8: provide stubs
#ifndef KODE_WITH_V8
namespace kode { namespace v8embed {

bool available() { return false; }
bool initialize(std::string* /*error_out*/) { return false; }
void shutdown() {}
void setRuntimeOptions(const RuntimeOptions& /*options*/) {}
void setEventLoop(uv_loop_t*) {}
uv_loop_t* eventLoop() { return nullptr; }
std::string runScript(const std::string& /*code*/, const std::string& /*filename*/, std::string* error_out) {
    if (error_out) *error_out = "V8 not available (build without KODE_WITH_V8)";
    return std::string();
}

}} // namespace
#else

// Build with V8
#include "kode_host.h"
#include "module_loader.h"
#include "v8_helpers.h"

#include <v8.h>
#include <libplatform/libplatform.h>
#include <memory>
#include <iostream>
#include <uv.h>

namespace kode { namespace v8embed {

static std::unique_ptr<v8::Platform> g_platform;
static v8::Isolate* g_isolate = nullptr;
static v8::ArrayBuffer::Allocator* g_allocator = nullptr;
static v8::Global<v8::Context> g_context;
static RuntimeOptions g_runtime_options;
static uv_loop_t* g_loop = nullptr;

// Console.log implementation
void ConsoleLogCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    
    for (int i = 0; i < args.Length(); i++) {
        v8::String::Utf8Value str(isolate, args[i]);
        if (i > 0) std::cout << " ";
        std::cout << *str;
    }
    std::cout << std::endl;
}

bool available() { return true; }

void setRuntimeOptions(const RuntimeOptions& options) {
    g_runtime_options = options;
}

void setEventLoop(uv_loop_t* loop) {
    g_loop = loop;
}

uv_loop_t* eventLoop() {
    return g_loop;
}

std::string PromiseResultToString(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context,
                                  v8::Local<v8::Value> value) {
    if (value->IsUndefined()) return std::string();
    v8::Context::Scope context_scope(context);
    v8::String::Utf8Value utf8(isolate, value);
    if (*utf8) return std::string(*utf8, utf8.length());
    return std::string();
}

std::string ResolveTopLevelPromise(v8::Local<v8::Context> context,
                                   v8::Local<v8::Value> result,
                                   std::string* error_out) {
    if (!result->IsPromise()) {
        return PromiseResultToString(g_isolate, context, result);
    }

    v8::Local<v8::Promise> promise = result.As<v8::Promise>();
    while (promise->State() == v8::Promise::kPending && g_loop && uv_loop_alive(g_loop)) {
        uv_run(g_loop, UV_RUN_ONCE);
        g_isolate->PerformMicrotaskCheckpoint();
    }

    if (promise->State() == v8::Promise::kRejected) {
        if (error_out) {
            std::string error = PromiseResultToString(g_isolate, context, promise->Result());
            *error_out = error.empty() ? "Promise rejected" : error;
        }
        return std::string();
    }
    if (promise->State() == v8::Promise::kFulfilled) {
        v8::Local<v8::Value> promise_result = promise->Result();
        if (promise_result->IsUndefined()) return std::string();
        return PromiseResultToString(g_isolate, context, promise_result);
    }
    return std::string();
}

bool initialize(std::string* error_out) {
    if (g_isolate) return true;
    
    v8::V8::InitializeICUDefaultLocation("./bin");
    
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
    g_isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);

    // Create a persistent context
    v8::Isolate::Scope isolate_scope(g_isolate);
    v8::HandleScope handle_scope(g_isolate);
    
    // Create global template
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(g_isolate);
    
    // Bind require
    global->Set(v8::String::NewFromUtf8(g_isolate, "require").ToLocalChecked(),
                v8::FunctionTemplate::New(g_isolate, RequireCallback));

    v8::Local<v8::Context> context = v8::Context::New(g_isolate, nullptr, global);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::Object> console = v8::Object::New(g_isolate);
    console->Set(context,
                 V8String(g_isolate, "log"),
                 v8::Function::New(context, ConsoleLogCallback).ToLocalChecked()).FromMaybe(false);
    context->Global()->Set(context, V8String(g_isolate, "console"), console).FromMaybe(false);

    CaptureEnvironment();
    if (!InstallKodeRuntimeBootstrap(g_isolate, context, error_out)) {
        return false;
    }
    if (!InstallKodeHostApis(g_isolate, context, g_runtime_options)) {
        if (error_out) *error_out = "Failed to install Kode host APIs";
        return false;
    }
    g_context.Reset(g_isolate, context);
    
    return true;
}

void shutdown() {
    if (!g_isolate) return;
    ClearModuleCache();
    ClearKodeHostState();
    g_loop = nullptr;
    g_context.Reset(); // Release context
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

std::string runScript(const std::string& code, const std::string& filename, std::string* error_out) {
    if (!g_isolate) {
        if (error_out) *error_out = "V8 not initialized";
        return std::string();
    }
    v8::Isolate::Scope isolate_scope(g_isolate);
    v8::HandleScope handle_scope(g_isolate);
    
    // Use the persistent context
    v8::Local<v8::Context> context = v8::Local<v8::Context>::New(g_isolate, g_context);
    SetModuleEntryDirectory(filename);

    const std::string normalized_filename = NormalizePath(filename);
    v8::Global<v8::Value> result_global;
    {
        v8::Context::Scope context_scope(context);
        v8::TryCatch try_catch(g_isolate);
        v8::Local<v8::String> source;
        if (!v8::String::NewFromUtf8(g_isolate, code.c_str(), v8::NewStringType::kNormal).ToLocal(&source)) {
            if (error_out) *error_out = "Failed to create V8 source string";
            return std::string();
        }

        v8::ScriptOrigin origin(V8String(g_isolate, normalized_filename));
        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
            if (error_out) {
                *error_out = FormatTryCatch(g_isolate, try_catch, normalized_filename);
            }
            return std::string();
        }

        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result)) {
            if (error_out) {
                *error_out = FormatTryCatch(g_isolate, try_catch, normalized_filename);
            }
            return std::string();
        }

        g_isolate->PerformMicrotaskCheckpoint();
        result_global.Reset(g_isolate, result);
    }

    v8::Local<v8::Value> result = v8::Local<v8::Value>::New(g_isolate, result_global);
    return ResolveTopLevelPromise(context, result, error_out);
}

}} // namespace
#endif
