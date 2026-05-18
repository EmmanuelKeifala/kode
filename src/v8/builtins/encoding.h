#pragma once

#include <v8.h>

namespace kode { namespace v8embed {

v8::Local<v8::Object> CreateEncodingModule(v8::Isolate* isolate, v8::Local<v8::Context> context);
bool InstallTextEncodingGlobals(v8::Isolate* isolate, v8::Local<v8::Context> context);
bool InstallKodeTextApi(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> kode);

} } // namespace kode::v8embed
