#include "engine_iface.h"

// Build without V8: provide stubs
#ifndef KODE_WITH_V8
namespace kode { namespace v8embed {

bool available() { return false; }
bool initialize(std::string* /*error_out*/) { return false; }
void shutdown() {}
void setRuntimeOptions(const RuntimeOptions& /*options*/) {}
std::string runScript(const std::string& /*code*/, const std::string& /*filename*/, std::string* error_out) {
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
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include "../filesystem/modern_fs.h"

extern char** environ;

namespace kode { namespace v8embed {

static std::unique_ptr<v8::Platform> g_platform;
static v8::Isolate* g_isolate = nullptr;
static v8::ArrayBuffer::Allocator* g_allocator = nullptr;
static v8::Global<v8::Context> g_context;
static std::unordered_map<std::string, v8::Global<v8::Value>> g_module_cache;
static std::vector<std::string> g_module_dir_stack;
static std::string g_entry_dir;
static RuntimeOptions g_runtime_options;
static std::unordered_map<std::string, std::string> g_env_snapshot;

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

std::string NormalizePath(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal().generic_string();
}

std::string Dirname(const std::string& path) {
    return std::filesystem::path(path).parent_path().generic_string();
}

std::string TrimTrailingSeparators(const std::string& path) {
    size_t end = path.size();
    while (end > 1 && path[end - 1] == '/') end--;
    return path.substr(0, end);
}

std::string LexicalPath(const std::filesystem::path& path) {
    std::string normalized = TrimTrailingSeparators(path.lexically_normal().generic_string());
    return normalized.empty() ? "." : normalized;
}

std::string PathDirname(const std::string& path) {
    std::string clean = TrimTrailingSeparators(path);
    if (clean == "/") return clean;
    std::string parent = std::filesystem::path(clean).parent_path().generic_string();
    return parent.empty() ? "." : parent;
}

std::string PathBasename(const std::string& path) {
    std::string clean = TrimTrailingSeparators(path);
    if (clean == "/") return clean;
    return std::filesystem::path(clean).filename().generic_string();
}

bool ReadStringArg(v8::Isolate* isolate,
                   v8::Local<v8::Context> context,
                   const v8::FunctionCallbackInfo<v8::Value>& args,
                   int index,
                   const std::string& operation,
                   std::string* out) {
    if (index >= args.Length() || !args[index]->IsString()) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", operation + " requires string arguments", operation, ""));
        return false;
    }
    v8::String::Utf8Value str(isolate, args[index]);
    *out = std::string(*str, str.length());
    return true;
}

bool IsLocalSpecifier(const std::string& specifier) {
    return specifier.rfind("./", 0) == 0 || specifier.rfind("../", 0) == 0;
}

bool ReadFileText(const std::string& path, std::string* out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    *out = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return true;
}

bool FileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::string ResolveLocalModulePath(const std::string& base_dir, const std::string& specifier) {
    std::string literal = NormalizePath(std::filesystem::path(base_dir) / specifier);
    if (FileExists(literal)) return literal;
    if (std::filesystem::path(literal).extension().empty()) {
        std::string with_js = literal + ".js";
        if (FileExists(with_js)) return with_js;
        return with_js;
    }
    return literal;
}

std::string FormatTryCatch(v8::Isolate* isolate, v8::TryCatch& try_catch, const std::string& fallback_file) {
    std::string file = fallback_file;
    int line = 0;
    v8::Local<v8::Message> message = try_catch.Message();
    if (!message.IsEmpty()) {
        v8::String::Utf8Value resource(isolate, message->GetScriptResourceName());
        if (resource.length() > 0) file = std::string(*resource, resource.length());
        line = message->GetLineNumber(isolate->GetCurrentContext()).FromMaybe(0);
    }
    v8::String::Utf8Value err(isolate, try_catch.Exception());
    std::string text = err.length() ? std::string(*err, err.length()) : "JavaScript error";
    if (line > 0) return file + ":" + std::to_string(line) + ": " + text;
    return file + ": " + text;
}

v8::Local<v8::Value> LoadLocalModule(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& specifier) {
    const std::string base_dir = !g_module_dir_stack.empty() ? g_module_dir_stack.back() : g_entry_dir;
    const std::string filename = ResolveLocalModulePath(base_dir, specifier);

    auto cached = g_module_cache.find(filename);
    if (cached != g_module_cache.end()) {
        v8::Local<v8::Value> cached_value = v8::Local<v8::Value>::New(isolate, cached->second);
        if (!cached_value->IsObject()) return cached_value;

        v8::Local<v8::Value> cached_exports;
        if (!cached_value.As<v8::Object>()->Get(context, V8String(isolate, "exports")).ToLocal(&cached_exports)) {
            return v8::Local<v8::Value>();
        }
        return cached_exports;
    }

    std::string source_text;
    if (!ReadFileText(filename, &source_text)) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EMODULE_NOT_FOUND", "Cannot find module '" + specifier + "'", "module.require", filename));
        return v8::Local<v8::Value>();
    }

    v8::Local<v8::Object> exports = v8::Object::New(isolate);
    v8::Local<v8::Object> module = v8::Object::New(isolate);
    module->Set(context, V8String(isolate, "exports"), exports).FromMaybe(false);
    g_module_cache[filename].Reset(isolate, module);

    std::string wrapped = "(function(exports, require, module, __filename, __dirname) {" + source_text;
    if (wrapped.empty() || wrapped.back() != '\n') wrapped += "\n";
    wrapped += "})";
    v8::ScriptOrigin origin(V8String(isolate, filename));
    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(context, V8String(isolate, wrapped), &origin).ToLocal(&script)) {
        g_module_cache.erase(filename);
        return v8::Local<v8::Value>();
    }

    v8::Local<v8::Value> wrapper_value;
    if (!script->Run(context).ToLocal(&wrapper_value) || !wrapper_value->IsFunction()) {
        g_module_cache.erase(filename);
        return v8::Local<v8::Value>();
    }

    v8::Local<v8::Value> require_value;
    if (!context->Global()->Get(context, V8String(isolate, "require")).ToLocal(&require_value) || !require_value->IsFunction()) {
        g_module_cache.erase(filename);
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINTERNAL", "Global require is not available", "module.require", filename));
        return v8::Local<v8::Value>();
    }

    const std::string dirname = Dirname(filename);
    v8::Local<v8::Value> argv[] = {
        exports,
        require_value,
        module,
        V8String(isolate, filename),
        V8String(isolate, dirname),
    };

    g_module_dir_stack.push_back(dirname);
    v8::Local<v8::Value> ignored;
    bool ok = wrapper_value.As<v8::Function>()->Call(context, context->Global(), 5, argv).ToLocal(&ignored);
    g_module_dir_stack.pop_back();
    if (!ok) {
        g_module_cache.erase(filename);
        return v8::Local<v8::Value>();
    }

    v8::Local<v8::Value> module_exports;
    if (!module->Get(context, V8String(isolate, "exports")).ToLocal(&module_exports)) {
        g_module_cache.erase(filename);
        return v8::Local<v8::Value>();
    }
    return module_exports;
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

bool GetSignalReasonIfAborted(v8::Isolate* isolate,
                              v8::Local<v8::Context> context,
                              v8::Local<v8::Value> options,
                              v8::Local<v8::Value>* reason_out) {
    if (options.IsEmpty() || !options->IsObject()) return false;
    v8::Local<v8::Object> object = options.As<v8::Object>();
    v8::Local<v8::Value> signal_value;
    if (!object->Get(context, V8String(isolate, "signal")).ToLocal(&signal_value) || !signal_value->IsObject()) return false;
    v8::Local<v8::Object> signal = signal_value.As<v8::Object>();
    v8::Local<v8::Value> aborted_value;
    if (!signal->Get(context, V8String(isolate, "aborted")).ToLocal(&aborted_value) || !aborted_value->BooleanValue(isolate)) return false;
    v8::Local<v8::Value> reason;
    if (!signal->Get(context, V8String(isolate, "reason")).ToLocal(&reason) || reason->IsUndefined()) {
        reason = CreateKodeError(isolate, context, "ECANCELED", "Operation cancelled", "Kode.timeout", "");
    }
    *reason_out = reason;
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

    v8::Local<v8::Value> cancelReason;
    if (args.Length() >= 2 && GetSignalReasonIfAborted(isolate, context, args[1], &cancelReason)) {
        RejectPromise(context, resolver, cancelReason);
        args.GetReturnValue().Set(resolver->GetPromise());
        return;
    }

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

    v8::Local<v8::Value> cancelReason;
    if (args.Length() >= 3 && GetSignalReasonIfAborted(isolate, context, args[2], &cancelReason)) {
        RejectPromise(context, resolver, cancelReason);
        args.GetReturnValue().Set(resolver->GetPromise());
        return;
    }

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

v8::Local<v8::Object> CreatePathModule(v8::Isolate* isolate, v8::Local<v8::Context> context);

// require implementation
void RequireCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EINVAL", "require requires a module string", "module.require", ""));
        return;
    }
    v8::String::Utf8Value str(isolate, args[0]);
    std::string moduleName(*str, str.length());
    
    if (moduleName == "kode:path") {
        args.GetReturnValue().Set(CreatePathModule(isolate, context));
    } else if (moduleName == "fs" || moduleName == "kode:fs") {
        v8::Local<v8::Object> fs = v8::Object::New(isolate);
        if (!fs->Set(context,
                v8::String::NewFromUtf8(isolate, "readFile").ToLocalChecked(),
                v8::Function::New(context, FSReadFileCallback).ToLocalChecked()).FromMaybe(false)) {
            isolate->ThrowException(v8::String::NewFromUtf8(isolate, "Failed to initialize fs module").ToLocalChecked());
            return;
        }
        if (!fs->Set(context,
                V8String(isolate, "readText"),
                v8::Function::New(context, FSReadTextCallback).ToLocalChecked()).FromMaybe(false)) {
            isolate->ThrowException(V8String(isolate, "Failed to initialize fs.readText"));
            return;
        }
        if (!fs->Set(context,
                V8String(isolate, "read"),
                v8::Function::New(context, FSReadCallback).ToLocalChecked()).FromMaybe(false)) {
            isolate->ThrowException(V8String(isolate, "Failed to initialize fs.read"));
            return;
        }
        if (!fs->Set(context,
                V8String(isolate, "write"),
                v8::Function::New(context, FSWriteCallback).ToLocalChecked()).FromMaybe(false)) {
            isolate->ThrowException(V8String(isolate, "Failed to initialize fs.write"));
            return;
        }
        if (!fs->Set(context,
                V8String(isolate, "info"),
                v8::Function::New(context, FSInfoCallback).ToLocalChecked()).FromMaybe(false)) {
            isolate->ThrowException(V8String(isolate, "Failed to initialize fs.info"));
            return;
        }
        args.GetReturnValue().Set(fs);
    } else if (IsLocalSpecifier(moduleName)) {
        v8::Local<v8::Value> module = LoadLocalModule(isolate, context, moduleName);
        if (!module.IsEmpty()) args.GetReturnValue().Set(module);
    } else {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EUNSUPPORTED_MODULE", "Unsupported module '" + moduleName + "'", "module.require", moduleName));
    }
}

void PathJoinCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::filesystem::path path;
    for (int i = 0; i < args.Length(); i++) {
        std::string part;
        if (!ReadStringArg(isolate, context, args, i, "kode:path.join", &part)) return;
        if (i > 0) {
            while (!part.empty() && (part[0] == '/' || part[0] == '\\')) {
                part.erase(0, 1);
            }
        }
        path /= part;
    }
    args.GetReturnValue().Set(V8String(isolate, LexicalPath(path)));
}

void PathNormalizeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string path;
    if (!ReadStringArg(isolate, context, args, 0, "kode:path.normalize", &path)) return;
    args.GetReturnValue().Set(V8String(isolate, LexicalPath(path)));
}

void PathDirnameCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string path;
    if (!ReadStringArg(isolate, context, args, 0, "kode:path.dirname", &path)) return;
    args.GetReturnValue().Set(V8String(isolate, PathDirname(path)));
}

void PathBasenameCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string path;
    if (!ReadStringArg(isolate, context, args, 0, "kode:path.basename", &path)) return;
    args.GetReturnValue().Set(V8String(isolate, PathBasename(path)));
}

void PathExtnameCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string path;
    if (!ReadStringArg(isolate, context, args, 0, "kode:path.extname", &path)) return;
    args.GetReturnValue().Set(V8String(isolate, std::filesystem::path(path).extension().generic_string()));
}

void PathIsAbsoluteCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string path;
    if (!ReadStringArg(isolate, context, args, 0, "kode:path.isAbsolute", &path)) return;
    args.GetReturnValue().Set(v8::Boolean::New(isolate, std::filesystem::path(path).is_absolute()));
}

void PathResolveCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::filesystem::path path = std::filesystem::current_path();
    for (int i = 0; i < args.Length(); i++) {
        std::string part;
        if (!ReadStringArg(isolate, context, args, i, "kode:path.resolve", &part)) return;
        std::filesystem::path next(part);
        path = next.is_absolute() ? next : path / next;
    }
    args.GetReturnValue().Set(V8String(isolate, LexicalPath(path)));
}

v8::Local<v8::Object> CreatePathModule(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> path = v8::Object::New(isolate);
    path->Set(context, V8String(isolate, "join"), v8::Function::New(context, PathJoinCallback).ToLocalChecked()).FromMaybe(false);
    path->Set(context, V8String(isolate, "normalize"), v8::Function::New(context, PathNormalizeCallback).ToLocalChecked()).FromMaybe(false);
    path->Set(context, V8String(isolate, "dirname"), v8::Function::New(context, PathDirnameCallback).ToLocalChecked()).FromMaybe(false);
    path->Set(context, V8String(isolate, "basename"), v8::Function::New(context, PathBasenameCallback).ToLocalChecked()).FromMaybe(false);
    path->Set(context, V8String(isolate, "extname"), v8::Function::New(context, PathExtnameCallback).ToLocalChecked()).FromMaybe(false);
    path->Set(context, V8String(isolate, "isAbsolute"), v8::Function::New(context, PathIsAbsoluteCallback).ToLocalChecked()).FromMaybe(false);
    path->Set(context, V8String(isolate, "resolve"), v8::Function::New(context, PathResolveCallback).ToLocalChecked()).FromMaybe(false);
    return path;
}

void CaptureEnvironment() {
    g_env_snapshot.clear();
    for (char** current = environ; current && *current; ++current) {
        std::string entry(*current);
        size_t equals = entry.find('=');
        if (equals == std::string::npos) continue;
        g_env_snapshot[entry.substr(0, equals)] = entry.substr(equals + 1);
    }
}

void FreezeValue(v8::Local<v8::Context> context, v8::Local<v8::Value> value) {
    if (!value->IsObject()) return;
    value.As<v8::Object>()->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false);
}

void EnvGetCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string name;
    if (!ReadStringArg(isolate, context, args, 0, "Kode.env.get", &name)) return;
    auto it = g_env_snapshot.find(name);
    if (it == g_env_snapshot.end()) return;
    args.GetReturnValue().Set(V8String(isolate, it->second));
}

void EnvHasCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string name;
    if (!ReadStringArg(isolate, context, args, 0, "Kode.env.has", &name)) return;
    args.GetReturnValue().Set(v8::Boolean::New(isolate, g_env_snapshot.find(name) != g_env_snapshot.end()));
}

void EnvToObjectCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> object = v8::Object::New(isolate);
    object->SetPrototype(context, v8::Null(isolate)).FromMaybe(false);
    for (const auto& entry : g_env_snapshot) {
        object->CreateDataProperty(context, V8String(isolate, entry.first), V8String(isolate, entry.second)).FromMaybe(false);
    }
    FreezeValue(context, object);
    args.GetReturnValue().Set(object);
}

bool InstallKodeHostApis(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Value> kode_value;
    if (!context->Global()->Get(context, V8String(isolate, "Kode")).ToLocal(&kode_value) || !kode_value->IsObject()) return false;
    v8::Local<v8::Object> kode = kode_value.As<v8::Object>();

    v8::Local<v8::Object> env = v8::Object::New(isolate);
    env->Set(context, V8String(isolate, "get"), v8::Function::New(context, EnvGetCallback).ToLocalChecked()).FromMaybe(false);
    env->Set(context, V8String(isolate, "has"), v8::Function::New(context, EnvHasCallback).ToLocalChecked()).FromMaybe(false);
    env->Set(context, V8String(isolate, "toObject"), v8::Function::New(context, EnvToObjectCallback).ToLocalChecked()).FromMaybe(false);
    FreezeValue(context, env);
    kode->Set(context, V8String(isolate, "env"), env).FromMaybe(false);

    v8::Local<v8::Array> values = v8::Array::New(isolate, static_cast<int>(g_runtime_options.args.size()));
    for (size_t i = 0; i < g_runtime_options.args.size(); i++) {
        values->Set(context, static_cast<uint32_t>(i), V8String(isolate, g_runtime_options.args[i])).FromMaybe(false);
    }
    FreezeValue(context, values);

    v8::Local<v8::Object> args = v8::Object::New(isolate);
    args->Set(context, V8String(isolate, "executable"), V8String(isolate, g_runtime_options.executable)).FromMaybe(false);
    v8::Local<v8::Value> script = g_runtime_options.script.empty()
        ? v8::Undefined(isolate).As<v8::Value>()
        : V8String(isolate, g_runtime_options.script).As<v8::Value>();
    args->Set(context, V8String(isolate, "script"), script).FromMaybe(false);
    args->Set(context, V8String(isolate, "values"), values).FromMaybe(false);
    FreezeValue(context, args);
    kode->Set(context, V8String(isolate, "args"), args).FromMaybe(false);
    FreezeValue(context, kode);
    if (!context->Global()
            ->DefineOwnProperty(context, V8String(isolate, "Kode"), kode,
                static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete))
            .FromMaybe(false)) {
        return false;
    }
    return true;
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

    timeout(ms) {
      if (typeof ms !== "number" || ms < 0) {
        throw runtimeError("EINVAL", "Kode.timeout requires a non-negative number", "Kode.timeout");
      }

      const signal = {
        aborted: false,
        reason: undefined,
        onabort: undefined,
      };

      function abort() {
        if (signal.aborted) return;
        signal.aborted = true;
        signal.reason = runtimeError("ECANCELED", "Operation cancelled", "Kode.timeout");
        if (typeof signal.onabort === "function") signal.onabort(signal.reason);
      }

      if (ms === 0) abort();

      return {
        signal,
        cancel() {
          abort();
        },
      };
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

void setRuntimeOptions(const RuntimeOptions& options) {
    g_runtime_options = options;
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
    if (!InstallKodeHostApis(g_isolate, context)) {
        if (error_out) *error_out = "Failed to install Kode host APIs";
        return false;
    }
    g_context.Reset(g_isolate, context);
    
    return true;
}

void shutdown() {
    if (!g_isolate) return;
    for (auto& entry : g_module_cache) {
        entry.second.Reset();
    }
    g_module_cache.clear();
    g_module_dir_stack.clear();
    g_entry_dir.clear();
    g_env_snapshot.clear();
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
    v8::Context::Scope context_scope(context);
    g_entry_dir = Dirname(NormalizePath(filename));

    v8::TryCatch try_catch(g_isolate);
    v8::Local<v8::String> source;
    if (!v8::String::NewFromUtf8(g_isolate, code.c_str(), v8::NewStringType::kNormal).ToLocal(&source)) {
        if (error_out) *error_out = "Failed to create V8 source string";
        return std::string();
    }

    const std::string normalized_filename = NormalizePath(filename);
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

    v8::String::Utf8Value utf8(g_isolate, result);
    if (*utf8) return std::string(*utf8, utf8.length());
    return std::string();
}

}} // namespace
#endif
