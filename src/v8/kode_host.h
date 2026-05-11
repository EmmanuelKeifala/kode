#pragma once

#include "engine_iface.h"

#include <string>
#include <v8.h>

namespace kode { namespace v8embed {

void CaptureEnvironment();
bool InstallKodeRuntimeBootstrap(v8::Isolate* isolate, v8::Local<v8::Context> context, std::string* error_out);
bool InstallKodeHostApis(v8::Isolate* isolate,
                         v8::Local<v8::Context> context,
                         const RuntimeOptions& runtime_options);
void ClearKodeHostState();

} } // namespace kode::v8embed
