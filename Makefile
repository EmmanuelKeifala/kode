CXX = ccache g++
OUTPUT_FILE=bin/kode

define INCLUDE
	v8/include/
endef

define INCLUDEUV
	libuv/include/
endef

define APP
	src/main.cc src/core/runtime.cc src/filesystem/fs.cc src/filesystem/modern_fs.cc src/parser/parser.cc src/concurrency/task.cc src/v8/engine.cc src/v8/v8_helpers.cc src/v8/builtins/path.cc src/v8/builtins/fs.cc src/v8/builtins/crypto.cc src/v8/builtins/encoding.cc src/v8/kode_host.cc src/v8/module_loader.cc
endef
 
define OBJ
	v8/libv8_monolith.a \
	libuv/libuv.a
endef

export INCLUDE
export INCLUDEUV
export OUTPUT_FILE

export APP
export LIB
export OBJ
export APP

examples=\
  cpp-native-threads\
  uv-threads\
  v8-print-hello

build:
	mkdir -p bin
	ccache g++ $(APP) -I $(INCLUDE) -I $(INCLUDEUV) -std=c++20 -pthread -o $(OUTPUT_FILE) -DKODE_WITH_V8 -DV8_COMPRESS_POINTERS $(OBJ) -Wl,--no-as-needed -ldl

test-concurrency:
	mkdir -p bin
	ccache g++ src/tests/concurrency_test.cc src/concurrency/task.cc -I $(INCLUDEUV) -std=c++20 -pthread -o bin/concurrency_test libuv/libuv.a -Wl,--no-as-needed -ldl

test-simple:
	mkdir -p bin
	ccache g++ src/tests/simple_concurrency_test.cc src/concurrency/task.cc -I $(INCLUDEUV) -std=c++20 -pthread -o bin/simple_test libuv/libuv.a -Wl,--no-as-needed -ldl

# Build and run HTTP server test (standalone C++)
test-http:
	mkdir -p bin
	ccache g++ src/tests/http/server_smoke.cc src/http/http_server.cc -I $(INCLUDEUV) -std=c++20 -pthread -o bin/http_test libuv/libuv.a -Wl,--no-as-needed -ldl

test-v8-microtask:
	mkdir -p bin
	ccache g++ src/tests/v8_microtask_test.cc -I $(INCLUDE) -std=c++20 -pthread -o bin/v8_microtask_test -DV8_COMPRESS_POINTERS v8/libv8_monolith.a -Wl,--no-as-needed -ldl
	./bin/v8_microtask_test

test-structured-runtime: build
	output="$$(./bin/kode tests/structured_scope_success.js)"; case "$$output" in *"alpha beta"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_sleep_basic.js)"; case "$$output" in *"sleep-basic true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_sleep_scope.js)"; case "$$output" in *"[object Promise]"*) printf '%s\n' "$$output"; exit 1;; *"sleep-scope alpha beta"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/top_level_scope_no_promise_echo.js)"; case "$$output" in *"[object Promise]"*) printf '%s\n' "$$output"; exit 1;; *"top-level-scope alpha beta"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/top_level_promise_rejection.js 2>&1; printf ' status:%s' "$$?")"; case "$$output" in *"status:0"*) printf '%s\n' "$$output"; exit 1;; *"top-level boom"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/structured_scope_failure.js)"; case "$$output" in *"caught boom"*"cancelled ECANCELED scope.async"*) case "$$output" in *"should-not-run"*) printf '%s\n' "$$output"; exit 1;; *) ;; esac ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_fs_read_text_success.js)"; case "$$output" in *"Kode is an experimental JavaScript runtime"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_fs_read_text_missing.js)"; case "$$output" in *"ENOENT fs.readText tests/does-not-exist.txt"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/structured_scope_diagnostics.js)"; case "$$output" in *"during true"*"done"*"after 0 0 0"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_fs_native_read.js)"; case "$$output" in *"read true file text/markdown"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_fs_native_read_missing.js)"; case "$$output" in *"read-missing ENOENT fs.read tests/does-not-exist-native.txt"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_fs_native_write_no_parents.js)"; case "$$output" in *"write-no-parents ENOENT fs.write tmp/kode-native/missing-parent/file.txt"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_fs_native_write_parents.js)"; case "$$output" in *"write-parents 5 file hello"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_fs_native_info.js)"; case "$$output" in *"info file text/markdown true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_timeout_basic.js)"; case "$$output" in *"timeout-before false true"*"timeout-after true ECANCELED Kode.timeout"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_fs_read_cancelled.js)"; case "$$output" in *"read-cancelled ECANCELED Kode.timeout"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_fs_write_cancelled.js)"; case "$$output" in *"write-cancelled ECANCELED Kode.timeout"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_require_exports.js)"; case "$$output" in *"module-add 5"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_module_exports.js)"; case "$$output" in *"module-reassign reassigned"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_cache.js)"; case "$$output" in *"module-cache true 1 1"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_nested.js)"; case "$$output" in *"module-nested nested"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_nested_require.js)"; case "$$output" in *"module-nested-require 10"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_meta.js)"; case "$$output" in *"module-meta true true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_missing.js)"; case "$$output" in *"module-missing EMODULE_NOT_FOUND module.require true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_unsupported.js)"; case "$$output" in *"module-unsupported EUNSUPPORTED_MODULE module.require"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_require_extensionless.js)"; case "$$output" in *"module-extensionless 13"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_nested_extensionless.js)"; case "$$output" in *"module-nested-extensionless 17"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_cycle.js)"; case "$$output" in *"module-cycle a-done a-start a-start"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_cycle_reassign_exports.js)"; case "$$output" in *"module-cycle-reassign current current captured true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_module_syntax_error.js 2>&1 || true)"; case "$$output" in *"bad_syntax.js:1"*|*"bad_syntax.js:2"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/modules/app_module_runtime_error.js 2>&1 || true)"; case "$$output" in *"runtime_error.js:1"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_path_basic.js)"; case "$$output" in *"path-join a/b/c.txt"*"path-normalize b/c.txt"*"path-dir-base a/b c.txt"*"path-ext-abs .txt true"*"path-normalize-parent a"*"path-resolve-parent /tmp"*"path-basename-trailing b"*"path-dirname-relative ."*"path-root / / /"*"path-join-absolute-segment a/b /a/b"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_path_no_bare_alias.js)"; case "$$output" in *"path-no-bare EUNSUPPORTED_MODULE module.require"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_crypto_hash_sha256.js)"; case "$$output" in *"crypto-sha256 sha256 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_crypto_hash_unsupported.js)"; case "$$output" in *"crypto-unsupported EUNSUPPORTED_ALGORITHM kode:crypto.hash"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_crypto_no_bare_alias.js)"; case "$$output" in *"crypto-no-bare EUNSUPPORTED_MODULE module.require"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_encoding_basic.js)"; case "$$output" in *"encoding-basic true 5 104 101 108 108 111 hello"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_text_basic.js)"; case "$$output" in *"kode-text true true hello"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/text_encoder_decoder_basic.js)"; case "$$output" in *"text-encoder-decoder true 5 104 111 hello"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/text_decoder_unsupported_label.js)"; case "$$output" in *"text-decoder-label EUNSUPPORTED_ENCODING TextDecoder"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/text_decoder_utf8_labels.js)"; case "$$output" in *"text-decoder-labels hi hi"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_encoding_unicode.js)"; case "$$output" in *"encoding-unicode true 8 true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_encoding_no_bare_alias.js)"; case "$$output" in *"encoding-no-bare EUNSUPPORTED_MODULE module.require"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_encoding_rejects_non_byte_view.js)"; case "$$output" in *"encoding-non-byte-view EINVAL kode:encoding.decodeUtf8"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(env KODE_ENV_TEST=hello '__proto__=sentinel' ./bin/kode tests/kode_env_basic.js)"; case "$$output" in *"env-has true"*"env-get hello"*"env-missing true"*"env-object hello"*"env-frozen true true"*"env-proto true sentinel"*"kode-global-protected function true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_args_basic.js alpha beta)"; case "$$output" in *"args-script true"*"args-values alpha,beta"*"args-frozen true true"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode -e 'console.log("args-e", Kode.args.script === undefined, Kode.args.values.join(","))' alpha beta)"; case "$$output" in *"args-e true alpha,beta"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/kode_no_process_global.js)"; case "$$output" in *"no-process undefined"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/parser_fallback_syntax_error.js 2>&1; printf ' status:%s' "$$?")"; case "$$output" in *"status:0"*|*"after syntax"*) printf '%s\n' "$$output"; exit 1;; *"parser_fallback_syntax_error.js"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(./bin/kode tests/parser_fallback_runtime_error.js 2>&1; printf ' status:%s' "$$?")"; case "$$output" in *"status:0"*) printf '%s\n' "$$output"; exit 1;; *"parser_fallback_runtime_error.js"*) ;; *) printf '%s\n' "$$output"; exit 1; esac
	output="$$(KODE_USE_V8=0 ./bin/kode tests/kode_no_process_global.js 2>&1; printf ' status:%s' "$$?")"; case "$$output" in *"status:0"*) printf '%s\n' "$$output"; exit 1;; *"V8 is required"*) ;; *) printf '%s\n' "$$output"; exit 1; esac

# Future: Build with V8 when we fix the compatibility issues
build-v8:
	mkdir -p bin
	$(CXX) $(APP) -I $(INCLUDE) -I $(INCLUDEUV) -std=c++20 -pthread -o bin/kode-v8 -DKODE_WITH_V8 -DV8_COMPRESS_POINTERS -DV8_ENABLE_SANDBOX $(OBJ) -Wl,--no-as-needed -ldl

$(examples): % : examples/%.cpp
	mkdir -p bin
	$(CXX) -I $$INCLUDE -I $(INCLUDEUV)  -std=c++17 -pthread -o bin/$@  $< -DV8_COMPRESS_POINTERS $$OBJ -Wl,--no-as-needed -ldl
	./bin/$@
clean:
	rm -rf bin
