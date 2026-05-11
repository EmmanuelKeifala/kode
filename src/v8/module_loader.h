#pragma once

#include <string>
#include <v8.h>

namespace kode { namespace v8embed {

void SetModuleEntryDirectory(const std::string& filename);
void ClearModuleCache();
void RequireCallback(const v8::FunctionCallbackInfo<v8::Value>& args);

} } // namespace kode::v8embed
