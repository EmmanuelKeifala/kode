#pragma once

#include <string>

namespace kode {
namespace v8embed {

bool available();
bool initialize(std::string* error_out = nullptr);
void shutdown();
std::string runScript(const std::string& code, const std::string& filename = "<eval>", std::string* error_out = nullptr);

} // namespace v8embed
} // namespace kode
