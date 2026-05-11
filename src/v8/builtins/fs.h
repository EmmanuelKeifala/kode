#pragma once

#include <v8.h>

namespace kode { namespace v8embed {

v8::Local<v8::Object> CreateFsModule(v8::Isolate* isolate, v8::Local<v8::Context> context);

} } // namespace kode::v8embed
