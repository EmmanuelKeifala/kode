#include "v8_helpers.h"

namespace kode { namespace v8embed {

v8::Local<v8::String> V8String(v8::Isolate* isolate, const char* value) {
    return v8::String::NewFromUtf8(isolate, value).ToLocalChecked();
}

v8::Local<v8::String> V8String(v8::Isolate* isolate, const std::string& value) {
    return v8::String::NewFromUtf8(isolate, value.c_str()).ToLocalChecked();
}

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

void FreezeValue(v8::Local<v8::Context> context, v8::Local<v8::Value> value) {
    if (!value->IsObject()) return;
    value.As<v8::Object>()->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false);
}

} } // namespace kode::v8embed
