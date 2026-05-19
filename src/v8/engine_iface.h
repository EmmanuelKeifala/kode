#pragma once

#include <string>
#include <vector>
#include <uv.h>

namespace kode {
namespace v8embed {

bool available();
bool initialize(std::string* error_out = nullptr);
void shutdown();

struct RuntimeOptions {
    std::string executable;
    std::string script;
    std::vector<std::string> args;
};

void setRuntimeOptions(const RuntimeOptions& options);
void setEventLoop(uv_loop_t* loop);
uv_loop_t* eventLoop();
std::string runScript(const std::string& code, const std::string& filename = "<eval>", std::string* error_out = nullptr);

} // namespace v8embed
} // namespace kode
