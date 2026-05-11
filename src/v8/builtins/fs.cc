#include "fs.h"

#include "../v8_helpers.h"
#include "../../filesystem/modern_fs.h"

#include <string>

namespace kode { namespace v8embed {

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

v8::Local<v8::Object> CreateFsModule(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> fs = v8::Object::New(isolate);
    fs->Set(context, V8String(isolate, "readFile"), v8::Function::New(context, FSReadFileCallback).ToLocalChecked()).FromMaybe(false);
    fs->Set(context, V8String(isolate, "readText"), v8::Function::New(context, FSReadTextCallback).ToLocalChecked()).FromMaybe(false);
    fs->Set(context, V8String(isolate, "read"), v8::Function::New(context, FSReadCallback).ToLocalChecked()).FromMaybe(false);
    fs->Set(context, V8String(isolate, "write"), v8::Function::New(context, FSWriteCallback).ToLocalChecked()).FromMaybe(false);
    fs->Set(context, V8String(isolate, "info"), v8::Function::New(context, FSInfoCallback).ToLocalChecked()).FromMaybe(false);
    return fs;
}

} } // namespace kode::v8embed
