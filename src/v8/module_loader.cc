#include "module_loader.h"

#include "builtins/crypto.h"
#include "builtins/encoding.h"
#include "builtins/fs.h"
#include "builtins/path.h"
#include "v8_helpers.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace kode { namespace v8embed {

static std::unordered_map<std::string, v8::Global<v8::Value>> g_module_cache;
static std::vector<std::string> g_module_dir_stack;
static std::string g_entry_dir;

void SetModuleEntryDirectory(const std::string& filename) {
    g_entry_dir = Dirname(NormalizePath(filename));
}

void ClearModuleCache() {
    for (auto& entry : g_module_cache) {
        entry.second.Reset();
    }
    g_module_cache.clear();
    g_module_dir_stack.clear();
    g_entry_dir.clear();
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
    } else if (moduleName == "kode:crypto") {
        args.GetReturnValue().Set(CreateCryptoModule(isolate, context));
    } else if (moduleName == "kode:encoding") {
        args.GetReturnValue().Set(CreateEncodingModule(isolate, context));
    } else if (moduleName == "fs" || moduleName == "kode:fs") {
        args.GetReturnValue().Set(CreateFsModule(isolate, context));
    } else if (IsLocalSpecifier(moduleName)) {
        v8::Local<v8::Value> module = LoadLocalModule(isolate, context, moduleName);
        if (!module.IsEmpty()) args.GetReturnValue().Set(module);
    } else {
        isolate->ThrowException(CreateKodeError(isolate, context,
            "EUNSUPPORTED_MODULE", "Unsupported module '" + moduleName + "'", "module.require", moduleName));
    }
}

} } // namespace kode::v8embed
