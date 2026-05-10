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
#include <sstream>
#include "../filesystem/modern_fs.h"

namespace kode { namespace v8embed {

static std::unique_ptr<v8::Platform> g_platform;
static v8::Isolate* g_isolate = nullptr;
static v8::ArrayBuffer::Allocator* g_allocator = nullptr;
static v8::Global<v8::Context> g_context;

v8::Local<v8::String> V8String(v8::Isolate* isolate, const char* value) {
    return v8::String::NewFromUtf8(isolate, value).ToLocalChecked();
}

v8::Local<v8::String> V8String(v8::Isolate* isolate, const std::string& value) {
    return v8::String::NewFromUtf8(isolate, value.c_str()).ToLocalChecked();
}

// Helper struct for async callbacks
struct AsyncReq {
    v8::Global<v8::Function> callback;
    v8::Global<v8::Context> context;
    v8::Isolate* isolate;
};

struct PromiseReq {
    v8::Global<v8::Promise::Resolver> resolver;
    v8::Global<v8::Context> context;
    v8::Isolate* isolate;
    std::string operation;
    std::string path;
};

v8::Local<v8::Promise::Resolver> NewResolver(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    return v8::Promise::Resolver::New(context).ToLocalChecked();
}

void ResolvePromise(v8::Local<v8::Context> context,
                    v8::Local<v8::Promise::Resolver> resolver,
                    v8::Local<v8::Value> value) {
    resolver->Resolve(context, value).FromMaybe(false);
}

void RejectPromise(v8::Local<v8::Context> context,
                   v8::Local<v8::Promise::Resolver> resolver,
                   v8::Local<v8::Value> value) {
    resolver->Reject(context, value).FromMaybe(false);
}

v8::Local<v8::Object> CreateKodeError(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& code,
                                      const std::string& message,
                                      const std::string& operation,
                                      const std::string& path) {
    v8::Local<v8::Object> error = v8::Exception::Error(V8String(isolate, message)).As<v8::Object>();
    error->Set(context, V8String(isolate, "code"), V8String(isolate, code)).FromMaybe(false);
    error->Set(context, V8String(isolate, "operation"), V8String(isolate, operation)).FromMaybe(false);
    error->Set(context, V8String(isolate, "path"), V8String(isolate, path)).FromMaybe(false);
    return error;
}

std::string FileKind(const ModernFS::FileInfo& info) {
    if (info.isFile) return "file";
    if (info.isDirectory) return "directory";
    return "other";
}

v8::Local<v8::Value> CreateInfoValue(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     const ModernFS::FileInfo& info) {
    if (info.path.empty()) return v8::Null(isolate);

    v8::Local<v8::Object> object = v8::Object::New(isolate);
    object->Set(context, V8String(isolate, "path"), V8String(isolate, info.path)).FromMaybe(false);
    object->Set(context, V8String(isolate, "kind"), V8String(isolate, FileKind(info))).FromMaybe(false);
    object->Set(context, V8String(isolate, "size"), v8::Number::New(isolate, static_cast<double>(info.size))).FromMaybe(false);
    object->Set(context, V8String(isolate, "mimeType"), V8String(isolate, info.mimeType)).FromMaybe(false);
    object->Set(context, V8String(isolate, "lastModified"), v8::Number::New(isolate, static_cast<double>(info.lastModified))).FromMaybe(false);
    return object;
}

bool GetStringOption(v8::Isolate* isolate,
                     v8::Local<v8::Context> context,
                     v8::Local<v8::Value> options,
                     const char* name,
                     std::string* out) {
    if (options.IsEmpty() || !options->IsObject()) return false;
    v8::Local<v8::Object> object = options.As<v8::Object>();
    v8::Local<v8::Value> value;
    if (!object->Get(context, V8String(isolate, name)).ToLocal(&value) || value->IsUndefined()) return false;
    if (!value->IsString()) return false;
    v8::String::Utf8Value str(isolate, value);
    *out = std::string(*str, str.length());
    return true;
}

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

void FSReadTextCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(V8String(isolate, "readText requires a path string"));
        return;
    }

    v8::String::Utf8Value filename(isolate, args[0]);
    std::string path(*filename, filename.length());
    v8::Local<v8::Promise::Resolver> resolver = NewResolver(isolate, context);

    auto* req = new PromiseReq();
    req->isolate = isolate;
    req->operation = "fs.readText";
    req->path = path;
    req->resolver.Reset(isolate, resolver);
    req->context.Reset(isolate, context);

    ModernFS::readFile(path, [req](const ModernFS::ReadResult& result) {
        v8::Isolate* isolate = req->isolate;
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, req->context);
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(isolate, req->resolver);

        if (result.success) {
            ResolvePromise(context, resolver, V8String(isolate, result.content));
        } else {
            v8::Local<v8::Object> error = CreateKodeError(isolate, context, "ENOENT", result.error, req->operation, req->path);
            RejectPromise(context, resolver, error);
        }

        isolate->PerformMicrotaskCheckpoint();
        req->resolver.Reset();
        req->context.Reset();
        delete req;
    });

    args.GetReturnValue().Set(resolver->GetPromise());
}

void FSReadCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(V8String(isolate, "fs.read requires a path string"));
        return;
    }

    std::string as = "text";
    if (args.Length() >= 2) {
        if (!args[1]->IsObject()) {
            isolate->ThrowException(V8String(isolate, "fs.read options must be an object"));
            return;
        }
        GetStringOption(isolate, context, args[1], "as", &as);
    }
    if (as != "text") {
        isolate->ThrowException(V8String(isolate, "fs.read only supports { as: \"text\" }"));
        return;
    }

    v8::String::Utf8Value filename(isolate, args[0]);
    std::string path(*filename, filename.length());
    v8::Local<v8::Promise::Resolver> resolver = NewResolver(isolate, context);

    auto* req = new PromiseReq();
    req->isolate = isolate;
    req->operation = "fs.read";
    req->path = path;
    req->resolver.Reset(isolate, resolver);
    req->context.Reset(isolate, context);

    ModernFS::readFile(path, [req](const ModernFS::ReadResult& result) {
        v8::Isolate* isolate = req->isolate;
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, req->context);
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(isolate, req->resolver);

        if (result.success) {
            v8::Local<v8::Object> file = v8::Object::New(isolate);
            file->Set(context, V8String(isolate, "text"), V8String(isolate, result.content)).FromMaybe(false);
            file->Set(context, V8String(isolate, "info"), CreateInfoValue(isolate, context, result.info)).FromMaybe(false);
            ResolvePromise(context, resolver, file);
        } else {
            v8::Local<v8::Object> error = CreateKodeError(isolate, context, "ENOENT", result.error, req->operation, req->path);
            RejectPromise(context, resolver, error);
        }

        isolate->PerformMicrotaskCheckpoint();
        req->resolver.Reset();
        req->context.Reset();
        delete req;
    });

    args.GetReturnValue().Set(resolver->GetPromise());
}

void FSWriteCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        isolate->ThrowException(V8String(isolate, "fs.write requires path and data strings"));
        return;
    }

    std::string create = "none";
    if (args.Length() >= 3) {
        if (!args[2]->IsObject()) {
            isolate->ThrowException(V8String(isolate, "fs.write options must be an object"));
            return;
        }
        GetStringOption(isolate, context, args[2], "create", &create);
    }
    if (create != "none" && create != "parents") {
        isolate->ThrowException(V8String(isolate, "fs.write create must be \"none\" or \"parents\""));
        return;
    }

    v8::String::Utf8Value filename(isolate, args[0]);
    v8::String::Utf8Value dataValue(isolate, args[1]);
    std::string path(*filename, filename.length());
    std::string data(*dataValue, dataValue.length());
    bool createParents = create == "parents";
    v8::Local<v8::Promise::Resolver> resolver = NewResolver(isolate, context);

    auto* req = new PromiseReq();
    req->isolate = isolate;
    req->operation = "fs.write";
    req->path = path;
    req->resolver.Reset(isolate, resolver);
    req->context.Reset(isolate, context);

    ModernFS::writeFile(path, data, "utf8", createParents, [req](const ModernFS::WriteResult& result) {
        v8::Isolate* isolate = req->isolate;
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, req->context);
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(isolate, req->resolver);

        if (result.success) {
            v8::Local<v8::Object> value = v8::Object::New(isolate);
            value->Set(context, V8String(isolate, "bytesWritten"), v8::Number::New(isolate, static_cast<double>(result.bytesWritten))).FromMaybe(false);
            value->Set(context, V8String(isolate, "info"), CreateInfoValue(isolate, context, result.info)).FromMaybe(false);
            ResolvePromise(context, resolver, value);
        } else {
            v8::Local<v8::Object> error = CreateKodeError(isolate, context, "ENOENT", result.error, req->operation, req->path);
            RejectPromise(context, resolver, error);
        }

        isolate->PerformMicrotaskCheckpoint();
        req->resolver.Reset();
        req->context.Reset();
        delete req;
    });

    args.GetReturnValue().Set(resolver->GetPromise());
}

void FSInfoCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(V8String(isolate, "fs.info requires a path string"));
        return;
    }

    v8::String::Utf8Value filename(isolate, args[0]);
    std::string path(*filename, filename.length());
    v8::Local<v8::Promise::Resolver> resolver = NewResolver(isolate, context);

    auto* req = new PromiseReq();
    req->isolate = isolate;
    req->operation = "fs.info";
    req->path = path;
    req->resolver.Reset(isolate, resolver);
    req->context.Reset(isolate, context);

    ModernFS::getFileInfo(path, [req](const ModernFS::FileInfo& info, const std::string& errorText) {
        v8::Isolate* isolate = req->isolate;
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, req->context);
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(isolate, req->resolver);

        if (!errorText.empty() && info.path.empty() && errorText.rfind("File not found:", 0) != 0) {
            v8::Local<v8::Object> error = CreateKodeError(isolate, context, "EIO", errorText, req->operation, req->path);
            RejectPromise(context, resolver, error);
        } else {
            ResolvePromise(context, resolver, CreateInfoValue(isolate, context, info));
        }

        isolate->PerformMicrotaskCheckpoint();
        req->resolver.Reset();
        req->context.Reset();
        delete req;
    });

    args.GetReturnValue().Set(resolver->GetPromise());
}

// require implementation
void RequireCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::String::Utf8Value str(isolate, args[0]);
    std::string moduleName(*str);
    
    if (moduleName == "fs" || moduleName == "kode:fs") {
        v8::Local<v8::Object> fs = v8::Object::New(isolate);
        if (!fs->Set(isolate->GetCurrentContext(),
                v8::String::NewFromUtf8(isolate, "readFile").ToLocalChecked(),
                v8::Function::New(isolate->GetCurrentContext(), FSReadFileCallback).ToLocalChecked()).FromMaybe(false)) {
            isolate->ThrowException(v8::String::NewFromUtf8(isolate, "Failed to initialize fs module").ToLocalChecked());
            return;
        }
        if (!fs->Set(isolate->GetCurrentContext(),
                V8String(isolate, "readText"),
                v8::Function::New(isolate->GetCurrentContext(), FSReadTextCallback).ToLocalChecked()).FromMaybe(false)) {
            isolate->ThrowException(V8String(isolate, "Failed to initialize fs.readText"));
            return;
        }
        if (!fs->Set(isolate->GetCurrentContext(),
                V8String(isolate, "read"),
                v8::Function::New(isolate->GetCurrentContext(), FSReadCallback).ToLocalChecked()).FromMaybe(false)) {
            isolate->ThrowException(V8String(isolate, "Failed to initialize fs.read"));
            return;
        }
        if (!fs->Set(isolate->GetCurrentContext(),
                V8String(isolate, "write"),
                v8::Function::New(isolate->GetCurrentContext(), FSWriteCallback).ToLocalChecked()).FromMaybe(false)) {
            isolate->ThrowException(V8String(isolate, "Failed to initialize fs.write"));
            return;
        }
        if (!fs->Set(isolate->GetCurrentContext(),
                V8String(isolate, "info"),
                v8::Function::New(isolate->GetCurrentContext(), FSInfoCallback).ToLocalChecked()).FromMaybe(false)) {
            isolate->ThrowException(V8String(isolate, "Failed to initialize fs.info"));
            return;
        }
        args.GetReturnValue().Set(fs);
    } else {
         isolate->ThrowException(v8::String::NewFromUtf8(isolate, ("Cannot find module '" + moduleName + "'").c_str()).ToLocalChecked());
    }
}

bool InstallKodeRuntimeBootstrap(v8::Isolate* isolate, v8::Local<v8::Context> context, std::string* error_out) {
    v8::Context::Scope context_scope(context);
    const char* source_code = R"JS(
(function(globalThis) {
  let activeScopes = 0;
  let activeTasks = 0;

  function runtimeError(code, message, operation) {
    const err = new Error(message);
    err.code = code;
    err.operation = operation;
    return err;
  }

  globalThis.Kode = {
    scope(fn) {
      if (typeof fn !== "function") {
        throw runtimeError("EINVAL", "Kode.scope requires a function", "Kode.scope");
      }

      activeScopes++;
      const state = { failed: false };
      const scope = {
        async(taskFn) {
          if (typeof taskFn !== "function") {
            throw runtimeError("EINVAL", "scope.async requires a function", "scope.async");
          }

          if (state.failed) {
            return Promise.reject(runtimeError("ECANCELED", "Scope already failed", "scope.async"));
          }

          activeTasks++;
          return Promise.resolve()
            .then(taskFn)
            .catch((err) => {
              state.failed = true;
              throw err;
            })
            .finally(() => {
              activeTasks--;
            });
        },
      };

      return Promise.resolve()
        .then(() => fn(scope))
        .finally(() => {
          activeScopes--;
        });
    },

    activeOperations() {
      return { scopes: activeScopes, tasks: activeTasks };
    },
  };
})(globalThis);
)JS";

    v8::TryCatch try_catch(isolate);
    v8::Local<v8::String> source = V8String(isolate, source_code);
    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(context, source).ToLocal(&script)) {
        if (error_out) {
            v8::String::Utf8Value err(isolate, try_catch.Exception());
            *error_out = err.length() ? *err : "Failed to compile Kode bootstrap";
        }
        return false;
    }

    v8::Local<v8::Value> result;
    if (!script->Run(context).ToLocal(&result)) {
        if (error_out) {
            v8::String::Utf8Value err(isolate, try_catch.Exception());
            *error_out = err.length() ? *err : "Failed to run Kode bootstrap";
        }
        return false;
    }

    return true;
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

    if (!InstallKodeRuntimeBootstrap(g_isolate, context, error_out)) {
        return false;
    }
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

    g_isolate->PerformMicrotaskCheckpoint();

    v8::String::Utf8Value utf8(g_isolate, result);
    if (*utf8) return std::string(*utf8, utf8.length());
    return std::string();
}

}} // namespace
#endif
