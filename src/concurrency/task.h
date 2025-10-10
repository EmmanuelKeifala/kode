#pragma once

#include <functional>
#include <memory>
#include <atomic>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <optional>
#include <ucontext.h>

// Forward declarations
class TaskScheduler;
template<typename T> class Channel;

// Lightweight task/fiber implementation inspired by Go goroutines
// Key features:
// - Tiny initial stacks (4KB) with dynamic growth
// - M:N scheduling (many tasks on few OS threads)
// - Preemptive scheduling with safepoints
// - CSP-style channels for communication

class Task {
public:
    friend class TaskScheduler;
    enum State {
        CREATED,
        RUNNABLE,
        RUNNING,
        BLOCKED,
        COMPLETED,
        CANCELLED
    };
    
    using TaskFunction = std::function<void()>;
    using TaskId = uint64_t;
    
private:
    TaskId id_;
    State state_;
    TaskFunction function_;
    std::vector<char> stack_;
    void* stack_pointer_;
    std::chrono::high_resolution_clock::time_point created_at_;
    std::chrono::high_resolution_clock::time_point last_run_;
    
    // ucontext-based fiber context (robust, portable on Linux)
    ucontext_t uctx_;
    bool uctx_initialized_ = false;
    
    // Full context switching support
    struct ExecutionContext {
        // CPU registers (x86_64)
        uint64_t rax, rbx, rcx, rdx;
        uint64_t rsi, rdi, rbp, rsp;
        uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
        uint64_t rip;  // instruction pointer
        uint64_t rflags;
        
        // FPU/SSE state
        alignas(16) char fpu_state[512];  // FXSAVE area
        
        // Stack information
        void* stack_base;
        void* stack_top;
        size_t stack_size;
    } context_;
    
    // Cancellation support
    std::atomic<bool> cancelled_;
    std::function<void()> cleanup_;
    
    // Stack management
    static constexpr size_t INITIAL_STACK_SIZE = 256 * 1024;  // 256KB initial fiber stack
    static constexpr size_t MAX_STACK_SIZE = 1024 * 1024;  // 1MB max
    
public:
    Task(TaskId id, TaskFunction func);
    ~Task();
    
    // Core task operations
    void run();
    void yield();
    void cancel();
    bool is_cancelled() const { return cancelled_.load(); }
    
    // Invoke the task function (used by fiber trampoline)
    void invoke();
    
    // State management
    State get_state() const { return state_; }
    void set_state(State state) { state_ = state; }
    TaskId get_id() const { return id_; }
    
    // Stack management
    bool grow_stack_if_needed();
    size_t get_stack_size() const { return stack_.size(); }
    
    // Timing
    auto get_age() const {
        return std::chrono::high_resolution_clock::now() - created_at_;
    }
    
    // Cleanup registration
    void set_cleanup(std::function<void()> cleanup) { cleanup_ = cleanup; }
    
    // Context switching (low-level)
    void save_context();
    void restore_context();
    static void context_switch(Task* from, Task* to);
    
private:
    void initialize_stack();
    bool check_stack_overflow();
    
    // Assembly helpers for context switching
    static void save_registers(ExecutionContext* ctx);
    static void restore_registers(const ExecutionContext* ctx);
    static void switch_stack(void* old_stack, void* new_stack);
};

// Go-style channel for task communication
template<typename T>
class Channel {
private:
    std::queue<T> buffer_;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    size_t capacity_;
    bool closed_;
    
public:
    explicit Channel(size_t capacity = 0) 
        : capacity_(capacity), closed_(false) {}
    
    ~Channel() { close(); }
    
    // Send value to channel (blocks if full)
    bool send(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (closed_) return false;
        
        // Block if channel is full (for buffered channels)
        if (capacity_ > 0) {
            not_full_.wait(lock, [this] { 
                return buffer_.size() < capacity_ || closed_; 
            });
            
            if (closed_) return false;
        }
        
        buffer_.push(value);
        not_empty_.notify_one();
        return true;
    }
    
    // Receive value from channel (blocks if empty)
    bool receive(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        not_empty_.wait(lock, [this] { 
            return !buffer_.empty() || closed_; 
        });
        
        if (buffer_.empty() && closed_) {
            return false;  // Channel closed and empty
        }
        
        value = buffer_.front();
        buffer_.pop();
        
        if (capacity_ > 0) {
            not_full_.notify_one();
        }
        
        return true;
    }
    
    // Try to send without blocking
    bool try_send(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (closed_) return false;
        
        if (capacity_ > 0 && buffer_.size() >= capacity_) {
            return false;  // Channel full
        }
        
        buffer_.push(value);
        not_empty_.notify_one();
        return true;
    }
    
    // Try to receive without blocking
    bool try_receive(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (buffer_.empty()) {
            return false;
        }
        
        value = buffer_.front();
        buffer_.pop();
        
        if (capacity_ > 0) {
            not_full_.notify_one();
        }
        
        return true;
    }
    
    // Close channel
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }
    
    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }
};

// Task scheduler with work-stealing and preemption
class TaskScheduler {
private:
    // Per-worker thread data
    struct WorkerThread {
        std::thread thread;
        std::queue<std::shared_ptr<Task>> local_queue;
        std::mutex queue_mutex;
        std::condition_variable queue_cv;
        std::atomic<bool> should_stop{false};
        std::atomic<bool> preemption_requested{false};
        size_t worker_id;
        
        // Current task tracking for preemption
        std::shared_ptr<Task> current_task;
        std::optional<std::chrono::high_resolution_clock::time_point> current_task_start_time;
        std::mutex current_task_mutex;  // protects current_task and its timing
        
        // Scheduler context for ucontext switching
        ucontext_t sched_ctx;
    };
    
    std::vector<std::unique_ptr<WorkerThread>> workers_;
    std::mutex workers_mutex_;  // protects workers_ during preemption and shutdown
    std::atomic<Task::TaskId> next_task_id_{1};
    std::atomic<size_t> active_tasks_{0};
    
    // Global task queue for work stealing
    std::queue<std::shared_ptr<Task>> global_queue_;
    std::mutex global_mutex_;
    
    // Task tracking for cancellation
    std::unordered_map<Task::TaskId, std::weak_ptr<Task>> active_task_map_;
    
    // Scheduler configuration
    size_t num_workers_;
    bool preemptive_scheduling_;
    std::chrono::milliseconds time_slice_{10};  // 10ms time slice
    
    // Preemption thread management
    std::thread preemption_thread_;
    std::atomic<bool> preemption_stop_{false};
    
public:
    TaskScheduler(size_t num_workers = 0);  // 0 = auto-detect CPU cores
    ~TaskScheduler();
    
    // Task management
    Task::TaskId spawn(Task::TaskFunction func);
    void yield_current_task();
    void cancel_task(Task::TaskId id);
    
    // Scheduler control
    void start();
    void stop();
    void wait_all();
    
    // Statistics
    size_t get_active_task_count() const { return active_tasks_.load(); }
    size_t get_worker_count() const { return num_workers_; }
    
    // Configuration
    void set_preemptive(bool enabled) { preemptive_scheduling_ = enabled; }
    void set_time_slice(std::chrono::milliseconds slice) { time_slice_ = slice; }
    
private:
    void worker_loop(size_t worker_id);
    std::shared_ptr<Task> steal_task(size_t worker_id);
    std::shared_ptr<Task> steal_task_fast(size_t worker_id);  // Optimized work stealing
    void schedule_task(std::shared_ptr<Task> task, size_t preferred_worker = SIZE_MAX);
    
    // Preemption support
    void setup_preemption();
    void preempt_long_running_tasks();
};

// High-level concurrency API for JavaScript integration
class ConcurrencyRuntime {
private:
    std::unique_ptr<TaskScheduler> scheduler_;
    
public:
    ConcurrencyRuntime();
    ~ConcurrencyRuntime();
    
    // Initialize concurrency runtime
    bool initialize(size_t num_workers = 0);
    void shutdown();
    
    // Kode-style API (inspired by Go)
    Task::TaskId kode(Task::TaskFunction func);  // spawn task
    void yield();                                // cooperative yield
    
    // Channel operations
    template<typename T>
    std::shared_ptr<Channel<T>> make_channel(size_t capacity = 0) {
        return std::make_shared<Channel<T>>(capacity);
    }
    
    // Structured concurrency
    void with_timeout(std::chrono::milliseconds timeout, Task::TaskFunction func);
    void join_all();
    
    // Statistics and monitoring
    void print_stats() const;
    size_t get_task_count() const;
};

// JavaScript API integration helpers
namespace JSConcurrency {
    // These will be exposed to JavaScript
    void spawn_task(const std::string& js_code);
    void create_channel(const std::string& name, size_t capacity);
    void send_to_channel(const std::string& channel_name, const std::string& value);
    std::string receive_from_channel(const std::string& channel_name);
    void yield_task();
}
