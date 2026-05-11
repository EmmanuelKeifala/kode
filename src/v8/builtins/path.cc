#include "path.h"

#include "../v8_helpers.h"

#include <filesystem>

namespace kode { namespace v8embed {

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

} } // namespace kode::v8embed
