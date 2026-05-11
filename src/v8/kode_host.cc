#include "kode_host.h"

#include "v8_helpers.h"

#include <unordered_map>

extern char** environ;

namespace kode { namespace v8embed {

static std::unordered_map<std::string, std::string> g_env_snapshot;

void CaptureEnvironment() {
    g_env_snapshot.clear();
    for (char** current = environ; current && *current; ++current) {
        std::string entry(*current);
        size_t equals = entry.find('=');
        if (equals == std::string::npos) continue;
        g_env_snapshot[entry.substr(0, equals)] = entry.substr(equals + 1);
    }
}

void EnvGetCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string name;
    if (!ReadStringArg(isolate, context, args, 0, "Kode.env.get", &name)) return;
    auto it = g_env_snapshot.find(name);
    if (it == g_env_snapshot.end()) return;
    args.GetReturnValue().Set(V8String(isolate, it->second));
}

void EnvHasCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string name;
    if (!ReadStringArg(isolate, context, args, 0, "Kode.env.has", &name)) return;
    args.GetReturnValue().Set(v8::Boolean::New(isolate, g_env_snapshot.find(name) != g_env_snapshot.end()));
}

void EnvToObjectCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> object = v8::Object::New(isolate);
    object->SetPrototype(context, v8::Null(isolate)).FromMaybe(false);
    for (const auto& entry : g_env_snapshot) {
        object->CreateDataProperty(context, V8String(isolate, entry.first), V8String(isolate, entry.second)).FromMaybe(false);
    }
    FreezeValue(context, object);
    args.GetReturnValue().Set(object);
}

bool InstallKodeHostApis(v8::Isolate* isolate,
                         v8::Local<v8::Context> context,
                         const RuntimeOptions& runtime_options) {
    v8::Local<v8::Value> kode_value;
    if (!context->Global()->Get(context, V8String(isolate, "Kode")).ToLocal(&kode_value) || !kode_value->IsObject()) return false;
    v8::Local<v8::Object> kode = kode_value.As<v8::Object>();

    v8::Local<v8::Object> env = v8::Object::New(isolate);
    env->Set(context, V8String(isolate, "get"), v8::Function::New(context, EnvGetCallback).ToLocalChecked()).FromMaybe(false);
    env->Set(context, V8String(isolate, "has"), v8::Function::New(context, EnvHasCallback).ToLocalChecked()).FromMaybe(false);
    env->Set(context, V8String(isolate, "toObject"), v8::Function::New(context, EnvToObjectCallback).ToLocalChecked()).FromMaybe(false);
    FreezeValue(context, env);
    kode->Set(context, V8String(isolate, "env"), env).FromMaybe(false);

    v8::Local<v8::Array> values = v8::Array::New(isolate, static_cast<int>(runtime_options.args.size()));
    for (size_t i = 0; i < runtime_options.args.size(); i++) {
        values->Set(context, static_cast<uint32_t>(i), V8String(isolate, runtime_options.args[i])).FromMaybe(false);
    }
    FreezeValue(context, values);

    v8::Local<v8::Object> args = v8::Object::New(isolate);
    args->Set(context, V8String(isolate, "executable"), V8String(isolate, runtime_options.executable)).FromMaybe(false);
    v8::Local<v8::Value> script = runtime_options.script.empty()
        ? v8::Undefined(isolate).As<v8::Value>()
        : V8String(isolate, runtime_options.script).As<v8::Value>();
    args->Set(context, V8String(isolate, "script"), script).FromMaybe(false);
    args->Set(context, V8String(isolate, "values"), values).FromMaybe(false);
    FreezeValue(context, args);
    kode->Set(context, V8String(isolate, "args"), args).FromMaybe(false);
    FreezeValue(context, kode);
    if (!context->Global()
            ->DefineOwnProperty(context, V8String(isolate, "Kode"), kode,
                static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete))
            .FromMaybe(false)) {
        return false;
    }
    return true;
}

bool InstallKodeRuntimeBootstrap(v8::Isolate* isolate, v8::Local<v8::Context> context, std::string* error_out) {
    v8::Context::Scope context_scope(context);
    const char* source_code = R"JS(
(function(globalThis) {
  let activeScopes = 0;
  let activeTasks = 0;

  function runtimeError(code, message, operation) {
    const err = new Error(message);
    err.code = code;
    err.operation = operation;
    return err;
  }

  globalThis.Kode = {
    scope(fn) {
      if (typeof fn !== "function") {
        throw runtimeError("EINVAL", "Kode.scope requires a function", "Kode.scope");
      }

      activeScopes++;
      const state = { failed: false };
      const scope = {
        async(taskFn) {
          if (typeof taskFn !== "function") {
            throw runtimeError("EINVAL", "scope.async requires a function", "scope.async");
          }

          if (state.failed) {
            return Promise.reject(runtimeError("ECANCELED", "Scope already failed", "scope.async"));
          }

          activeTasks++;
          return Promise.resolve()
            .then(taskFn)
            .catch((err) => {
              state.failed = true;
              throw err;
            })
            .finally(() => {
              activeTasks--;
            });
        },
      };

      return Promise.resolve()
        .then(() => fn(scope))
        .finally(() => {
          activeScopes--;
        });
    },

    activeOperations() {
      return { scopes: activeScopes, tasks: activeTasks };
    },

    timeout(ms) {
      if (typeof ms !== "number" || ms < 0) {
        throw runtimeError("EINVAL", "Kode.timeout requires a non-negative number", "Kode.timeout");
      }

      const signal = {
        aborted: false,
        reason: undefined,
        onabort: undefined,
      };

      function abort() {
        if (signal.aborted) return;
        signal.aborted = true;
        signal.reason = runtimeError("ECANCELED", "Operation cancelled", "Kode.timeout");
        if (typeof signal.onabort === "function") signal.onabort(signal.reason);
      }

      if (ms === 0) abort();

      return {
        signal,
        cancel() {
          abort();
        },
      };
    },
  };
})(globalThis);
)JS";

    v8::TryCatch try_catch(isolate);
    v8::Local<v8::String> source = V8String(isolate, source_code);
    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(context, source).ToLocal(&script)) {
        if (error_out) {
            v8::String::Utf8Value err(isolate, try_catch.Exception());
            *error_out = err.length() ? *err : "Failed to compile Kode bootstrap";
        }
        return false;
    }

    v8::Local<v8::Value> result;
    if (!script->Run(context).ToLocal(&result)) {
        if (error_out) {
            v8::String::Utf8Value err(isolate, try_catch.Exception());
            *error_out = err.length() ? *err : "Failed to run Kode bootstrap";
        }
        return false;
    }

    return true;
}

void ClearKodeHostState() {
    g_env_snapshot.clear();
}

} } // namespace kode::v8embed
