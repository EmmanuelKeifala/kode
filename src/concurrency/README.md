# Kode Concurrency Runtime

This directory contains the C++ concurrency runtime that powers Kode. It provides lightweight user-space fibers, an M:N task scheduler, work-stealing between workers, cooperative yielding, and CSP-style channels.

The runtime is designed for both direct C++ use and JS integration via the Kode runtime.

## Architecture

- **Fibers (user-space tasks)**: Implemented with `ucontext`, each `Task` has its own stack. `swapcontext` is used to switch between the scheduler and tasks.
- **M:N scheduler**: A small number of OS threads (workers) run many tasks. Per-worker local queues with a global queue for balancing.
- **Work stealing**: Idle workers steal from others using a fast, low-contention strategy.
- **Cooperative rescheduling**: A timer can request rescheduling, but tasks must hit safepoints (e.g., `yield()`, channel ops, I/O) before another task runs.
- **Channels**: CSP-style channels (`Channel<T>`) support buffered and unbuffered communication.
- **Clean shutdown**: Managed preemption thread and worker threads with safe shutdown.

Logging level can be controlled via the environment variable `KODE_LOG_LEVEL` with values `error`, `info`, or `debug`.

## C++ APIs

All APIs are declared in `src/concurrency/task.h`.

### Task

- `class Task`
  - Represents a user-space fiber.
  - Important methods (used by the scheduler internally): `yield()`, `cancel()`, `get_id()`, `get_state()`.

### Channel

- `template<typename T> class Channel<T>`
  - `Channel(size_t capacity = 0)`: capacity 0 creates an unbuffered channel.
  - `bool send(const T& value)`: blocks if buffered channel is full.
  - `bool receive(T& value)`: blocks if empty; returns false if closed and empty.
  - `bool try_send(const T& value)`, `bool try_receive(T& value)`: non-blocking variants.
  - `void close()`: closes the channel.

Use `ConcurrencyRuntime::make_channel<T>(...)` to create channels for tasks scheduled through the runtime.

### TaskScheduler

- `class TaskScheduler`
  - `TaskScheduler(size_t num_workers = 0)`: 0 selects hardware concurrency or a fallback.
  - `Task::TaskId spawn(Task::TaskFunction func)`: enqueue a new task.
  - `void yield_current_task()`: cooperative yield for the current running task.
  - `void cancel_task(Task::TaskId id)`: request cancellation.
  - `void start()`, `void stop()`: start/stop worker threads.
  - `void wait_all()`: block until all active tasks complete.
- `void set_preemptive(bool enabled)`: enable/disable rescheduling requests.
  - `void set_time_slice(std::chrono::milliseconds slice)`: configure preemption request interval.

### ConcurrencyRuntime (high-level C++ API)

- `class ConcurrencyRuntime`
  - `bool initialize(size_t num_workers = 0)`
  - `void shutdown()`
  - `Task::TaskId kode(Task::TaskFunction func)`
  - `void yield()`
  - `void with_timeout(std::chrono::milliseconds timeout, Task::TaskFunction func)`
  - `void join_all()`
  - `template<typename T> std::shared_ptr<Channel<T>> make_channel(size_t capacity = 0)`
  - `size_t get_task_count() const`, `void print_stats() const`

## C++ Usage Examples

### Basic task spawning

```cpp
#include "concurrency/task.h"
#include <iostream>

int main() {
    ConcurrencyRuntime rt;
    rt.initialize(2);

    rt.kode([](){ std::cout << "task 1" << std::endl; });
    rt.kode([](){ std::cout << "task 2" << std::endl; });

    rt.join_all();
    rt.shutdown();
}
```

### Channels

```cpp
#include "concurrency/task.h"
#include <iostream>
#include <string>

int main() {
    ConcurrencyRuntime rt;
    rt.initialize(2);

    auto ch = rt.make_channel<std::string>(2);

    rt.kode([ch](){ ch->send("hello"); ch->send("world"); });
    rt.kode([ch](){
        std::string s;
        while (ch->receive(s)) {
            std::cout << s << std::endl;
            if (s == "world") ch->close();
        }
    });

    rt.join_all();
    rt.shutdown();
}
```

### Cooperative yielding and timeout

```cpp
#include "concurrency/task.h"
#include <chrono>
#include <thread>

int main() {
    ConcurrencyRuntime rt;
    rt.initialize(1);

    rt.kode([&rt](){
        // Long loop with safepoints
        for (int i = 0; i < 5; ++i) {
            // do work
            rt.yield(); // cooperative yield
        }
    });

    rt.with_timeout(std::chrono::milliseconds(100), [](){
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    rt.join_all();
    rt.shutdown();
}
```

## JS Integration

The JS-facing API is parsed by `src/parser/parser.cc` and executed through `KodeRuntime` in `src/core/runtime.cc`.

Supported statements:

- `kode("...")`: spawn a concurrent task that executes the inner JS snippet.
- `createChannel(name, capacity)`
- `sendToChannel(name, value)`
- `receiveFromChannel(name)`
- `yield()`
- `withTimeout(ms, "...snippet...")`

Example (`src/tests/concurrency_test.js`):

```javascript
console.log("=== Kode Concurrency Tests ===");

// Basic spawning
kode("console.log('Task 1 executed')");
kode("console.log('Task 2 executed')");

// Channels
createChannel("test", 2);
kode("sendToChannel('test', 'hello')");
kode("receiveFromChannel('test')");

// Yield
kode("console.log('Before yield')");
kode("yield()");
kode("console.log('After yield')");

// Timeout
withTimeout(100, "console.log('within 100ms')");
```

Notes:

- Use one meaningful operation per `kode("...")` call. Avoid chaining multiple statements in a single string.
- Channel operations executed via `kode("...")` are interpreted by the parser and routed to the concurrency runtime.
- File system operations inside `kode("...")` use the runtime FS layer; see `src/filesystem/` for details.

## Build and Test

From the project root:

- **Build runtime**:

```
make build
```

- **C++ tests**:

```
make test-concurrency
./bin/concurrency_test
```

- **Run JS test**:

```
./bin/kode ./src/tests/concurrency_test.js
```

- **Logging**:

```
KODE_LOG_LEVEL=error ./bin/kode ./src/tests/concurrency_test.js
KODE_LOG_LEVEL=debug ./bin/concurrency_test
```

## Operational Guidance

- **Safepoints**: Insert `yield()` in long-running loops or use channel/FS operations to provide natural safepoints.
- **Cancellation**: `with_timeout` requests cancellation; blocking work that does not observe cancellation may still run to completion.
- **Timeouts**: Use `with_timeout` (C++) or `withTimeout` (JS) to bound task execution lifetimes.
- **Shutdown**: Always drain work (`join_all`) before `shutdown`.
- **Capacity Planning**: Tune worker count in `initialize(num_workers)` and the time slice via `set_time_slice(...)` if needed.

Refer to the source for the most accurate signatures:

- Scheduler and primitives: `src/concurrency/task.h`, `src/concurrency/task.cc`
- Kode runtime integration: `src/core/runtime.h`, `src/core/runtime.cc`
- JS parser and statements: `src/parser/parser.h`, `src/parser/parser.cc`
