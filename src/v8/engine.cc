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
#include <iostream>
#include <fstream>
#include "../filesystem/modern_fs.h"

namespace kode { namespace v8embed {

static std::unique_ptr<v8::Platform> g_platform;
static v8::Isolate* g_isolate = nullptr;
static v8::ArrayBuffer::Allocator* g_allocator = nullptr;
static v8::Global<v8::Context> g_context;

// Helper struct for async callbacks
struct AsyncReq {
    v8::Global<v8::Function> callback;
    v8::Global<v8::Context> context;
    v8::Isolate* isolate;
};

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

// fs.readFile implementation
void FSReadFileCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
        isolate->ThrowException(v8::String::NewFromUtf8(isolate, "Invalid arguments").ToLocalChecked());
        return;
    }

    v8::String::Utf8Value filename(isolate, args[0]);
    std::string path(*filename);
    
    auto* req = new AsyncReq();
    req->isolate = isolate;
    req->callback.Reset(isolate, v8::Local<v8::Function>::Cast(args[1]));
    req->context.Reset(isolate, isolate->GetCurrentContext());
    
    // Call ModernFS async
    ModernFS::readFile(path, [req](const ModernFS::ReadResult& result) {
        v8::Isolate* isolate = req->isolate;
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, req->context);
        v8::Context::Scope context_scope(context);
        
        v8::Local<v8::Value> argv[2];
        if (result.success) {
            argv[0] = v8::Null(isolate);
            argv[1] = v8::String::NewFromUtf8(isolate, result.content.c_str()).ToLocalChecked();
        } else {
            argv[0] = v8::String::NewFromUtf8(isolate, result.error.c_str()).ToLocalChecked();
            argv[1] = v8::Null(isolate);
        }
        
        v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(isolate, req->callback);
        // Call the callback
        if (!callback->Call(context, context->Global(), 2, argv).IsEmpty()) {
            // Success
        }
        
        // Cleanup global handles
        req->callback.Reset();
        req->context.Reset();
        delete req;
    });
}

// require implementation
void RequireCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::String::Utf8Value str(isolate, args[0]);
    std::string moduleName(*str);
    
    if (moduleName == "fs") {
        v8::Local<v8::Object> fs = v8::Object::New(isolate);
        fs->Set(isolate->GetCurrentContext(), 
            v8::String::NewFromUtf8(isolate, "readFile").ToLocalChecked(),
            v8::Function::New(isolate->GetCurrentContext(), FSReadFileCallback).ToLocalChecked());
        args.GetReturnValue().Set(fs);
    } else {
         isolate->ThrowException(v8::String::NewFromUtf8(isolate, ("Cannot find module '" + moduleName + "'").c_str()).ToLocalChecked());
    }
}

bool available() { return true; }

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

    // Create a persistent context
    v8::Isolate::Scope isolate_scope(g_isolate);
    v8::HandleScope handle_scope(g_isolate);
    
    // Create global template
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(g_isolate);
    
    // Bind console.log
    v8::Local<v8::ObjectTemplate> console = v8::ObjectTemplate::New(g_isolate);
    console->Set(v8::String::NewFromUtf8(g_isolate, "log").ToLocalChecked(),
                 v8::FunctionTemplate::New(g_isolate, ConsoleLogCallback));
    global->Set(v8::String::NewFromUtf8(g_isolate, "console").ToLocalChecked(), console);
    
    // Bind require
    global->Set(v8::String::NewFromUtf8(g_isolate, "require").ToLocalChecked(),
                v8::FunctionTemplate::New(g_isolate, RequireCallback));

    v8::Local<v8::Context> context = v8::Context::New(g_isolate, nullptr, global);
    g_context.Reset(g_isolate, context);
    
    return true;
}

void shutdown() {
    if (!g_isolate) return;
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

std::string runScript(const std::string& code, std::string* error_out) {
    if (!g_isolate) {
        if (error_out) *error_out = "V8 not initialized";
        return std::string();
    }
    v8::Isolate::Scope isolate_scope(g_isolate);
    v8::HandleScope handle_scope(g_isolate);
    
    // Use the persistent context
    v8::Local<v8::Context> context = v8::Local<v8::Context>::New(g_isolate, g_context);
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
