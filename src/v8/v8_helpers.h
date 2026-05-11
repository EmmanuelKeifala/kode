#pragma once

#include <filesystem>
#include <string>
#include <v8.h>

namespace kode { namespace v8embed {

v8::Local<v8::String> V8String(v8::Isolate* isolate, const char* value);
v8::Local<v8::String> V8String(v8::Isolate* isolate, const std::string& value);

v8::Local<v8::Object> CreateKodeError(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& code,
                                      const std::string& message,
                                      const std::string& operation,
                                      const std::string& path);

std::string NormalizePath(const std::filesystem::path& path);
std::string Dirname(const std::string& path);
std::string TrimTrailingSeparators(const std::string& path);
std::string LexicalPath(const std::filesystem::path& path);
std::string PathDirname(const std::string& path);
std::string PathBasename(const std::string& path);

bool ReadStringArg(v8::Isolate* isolate,
                   v8::Local<v8::Context> context,
                   const v8::FunctionCallbackInfo<v8::Value>& args,
                   int index,
                   const std::string& operation,
                   std::string* out);

v8::Local<v8::Promise::Resolver> NewResolver(v8::Isolate* isolate, v8::Local<v8::Context> context);
void ResolvePromise(v8::Local<v8::Context> context,
                    v8::Local<v8::Promise::Resolver> resolver,
                    v8::Local<v8::Value> value);
void RejectPromise(v8::Local<v8::Context> context,
                   v8::Local<v8::Promise::Resolver> resolver,
                   v8::Local<v8::Value> value);

std::string FormatTryCatch(v8::Isolate* isolate, v8::TryCatch& try_catch, const std::string& fallback_file);
void FreezeValue(v8::Local<v8::Context> context, v8::Local<v8::Value> value);

} } // namespace kode::v8embed
