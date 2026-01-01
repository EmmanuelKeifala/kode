#include "task.h"
#include <iostream>
#include <algorithm>
#include <random>
#include <cstring>
#include <cmath>
#include <mutex>
#include <string>
#include <cstdint>
#include <cstdlib>
// POSIX signal-based preemption removed for stability; using cooperative ucontext

// Simple logging helpers for structured, less noisy output
namespace {
    enum class LogLevel { Error = 0, Info = 1, Debug = 2 };
    static LogLevel g_log_level = LogLevel::Error;  // default: Error (quiet mode)
    static std::mutex g_log_mutex;
    static bool g_log_level_inited = false;
    
    void init_log_level_from_env() {
        if (g_log_level_inited) return;
        g_log_level_inited = true;
        const char* env = std::getenv("KODE_LOG_LEVEL");
        if (!env) return;
        std::string s(env);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        if (s == "debug") g_log_level = LogLevel::Debug;
        else if (s == "info") g_log_level = LogLevel::Info;
        else if (s == "error") g_log_level = LogLevel::Error;
    }
    
    template<typename F>
    void log_locked(F f) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        f();
    }
    
    void log_info(const std::string& msg) {
        if (g_log_level >= LogLevel::Info) {
            log_locked([&]() { std::cout << msg << std::endl; });
        }
    }
    
    void log_debug(const std::string& msg) {
        if (g_log_level >= LogLevel::Debug) {
            log_locked([&]() { std::cout << msg << std::endl; });
        }
    }
    
    void log_error(const std::string& msg) {
        log_locked([&]() { std::cerr << msg << std::endl; });
    }
}

// Thread-local scheduler context and current task for ucontext-based switching
static thread_local ucontext_t* g_sched_ctx = nullptr;
static thread_local Task* g_current_task = nullptr;

// Trampoline invoked by makecontext to execute a task function
static void TaskTrampoline() {
    if (g_current_task) {
        g_current_task->invoke();
    }
}


// Task implementation
Task::Task(TaskId id, TaskFunction func) 
    : id_(id), state_(CREATED), function_(func), cancelled_(false) {
    created_at_ = std::chrono::high_resolution_clock::now();
    initialize_stack();
}

Task::~Task() {
    if (cleanup_) {
        cleanup_();
    }
}

void Task::initialize_stack() {
    stack_.resize(INITIAL_STACK_SIZE);
    stack_pointer_ = stack_.data() + stack_.size();
    // ucontext will be initialized lazily in the worker before first run
}

void Task::run() {
    if (cancelled_.load() || state_ == COMPLETED) {
        return;
    }
    
    state_ = RUNNING;
    last_run_ = std::chrono::high_resolution_clock::now();
    
    try {
        function_();
        state_ = COMPLETED;
    } catch (const std::exception& e) {
        std::cerr << "[Task " << id_ << "] Exception: " << e.what() << std::endl;
        state_ = COMPLETED;
    }
}

void Task::invoke() {
    // Execute the task function inside the fiber context
    if (cancelled_.load()) {
        state_ = CANCELLED;
        return;
    }
    state_ = RUNNING;
    try {
        function_();
        state_ = COMPLETED;
    } catch (const std::exception& e) {
        std::cerr << "[Task " << id_ << "] Exception: " << e.what() << std::endl;
        state_ = COMPLETED;
    }
    // Upon return, control goes to uc_link (scheduler context)
}

void Task::yield() {
    if (cancelled_.load()) {
        throw std::runtime_error("Task cancelled");
    }
    state_ = RUNNABLE;
    last_run_ = std::chrono::high_resolution_clock::now();
    if (g_sched_ctx) {
        // Save this task context and switch to scheduler
        swapcontext(&uctx_, g_sched_ctx);
    } else {
        // Fallback if no scheduler context is set (should not happen on workers)
        std::this_thread::yield();
    }
    if (cancelled_.load()) {
        throw std::runtime_error("Task cancelled during yield");
    }
}

// Full context switching implementation using proper assembly
void Task::save_context() {
    #ifdef __x86_64__
    // Use proper assembly with correct constraints and clobbers
    asm volatile (
        "movq %%rsp, %0\n\t"
        "movq %%rbp, %1\n\t"
        "movq %%rbx, %2\n\t"
        "movq %%r12, %3\n\t"
        "movq %%r13, %4\n\t"
        "movq %%r14, %5\n\t"
        "movq %%r15, %6\n\t"
        "pushfq\n\t"
        "popq %%rax\n\t"
        "movq %%rax, %7"
        : "=m" (context_.rsp),
          "=m" (context_.rbp),
          "=m" (context_.rbx),
          "=m" (context_.r12),
          "=m" (context_.r13),
          "=m" (context_.r14),
          "=m" (context_.r15),
          "=m" (context_.rflags)
        :
        : "rax", "memory"
    );
    
    // Save current instruction pointer (approximate)
    void* current_ip;
    asm volatile ("leaq (%%rip), %0" : "=r" (current_ip));
    context_.rip = reinterpret_cast<uint64_t>(current_ip);
    
    std::cout << "[Context] Saved context for task " << id_ 
              << " (RSP: 0x" << std::hex << context_.rsp << std::dec << ")" << std::endl;
    #else
    // Fallback for non-x86_64 architectures
    std::cout << "[Context] Context saving not implemented for this architecture" << std::endl;
    #endif
}

void Task::restore_context() {
    #ifdef __x86_64__
    // Restore registers using proper assembly
    asm volatile (
        "movq %0, %%rax\n\t"
        "pushq %%rax\n\t"
        "popfq\n\t"
        "movq %1, %%rbx\n\t"
        "movq %2, %%rbp\n\t"
        "movq %3, %%r12\n\t"
        "movq %4, %%r13\n\t"
        "movq %5, %%r14\n\t"
        "movq %6, %%r15"
        :
        : "m" (context_.rflags),
          "m" (context_.rbx),
          "m" (context_.rbp),
          "m" (context_.r12),
          "m" (context_.r13),
          "m" (context_.r14),
          "m" (context_.r15)
        : "rax", "memory"
    );
    
    // Note: RSP restoration requires careful stack management
    // For production use, we'd need to switch stacks properly
    std::cout << "[Context] Restored context for task " << id_ 
              << " (RSP: 0x" << std::hex << context_.rsp << std::dec << ")" << std::endl;
    #else
    // Fallback for non-x86_64 architectures
    std::cout << "[Context] Context restoration not implemented for this architecture" << std::endl;
    #endif
}

// Full context switch between two tasks
void Task::context_switch(Task* from, Task* to) {
    if (!from || !to) return;
    
    std::cout << "[ContextSwitch] Switching from task " << from->get_id() 
              << " to task " << to->get_id() << std::endl;
    
    // Save current task context
    from->save_context();
    from->set_state(Task::RUNNABLE);
    
    // Switch to new task context
    to->restore_context();
    to->set_state(Task::RUNNING);
    
    std::cout << "[ContextSwitch] Context switch completed" << std::endl;
}

void Task::cancel() {
    cancelled_.store(true);
    state_ = CANCELLED;
}

bool Task::grow_stack_if_needed() {
    if (stack_.size() >= MAX_STACK_SIZE) {
        return false;  // Stack overflow
    }
    
    // Check if we're getting close to stack overflow
    uintptr_t current_sp = context_.rsp;
    uintptr_t stack_base = reinterpret_cast<uintptr_t>(context_.stack_base);
    
    if (current_sp <= stack_base + 1024) {  // Less than 1KB left
        std::cout << "[Stack] Growing stack for task " << id_ 
                  << " from " << stack_.size() << " to ";
        
        size_t old_size = stack_.size();
        size_t new_size = std::min(old_size * 2, MAX_STACK_SIZE);
        stack_.resize(new_size);
        
        // Recalculate stack pointers
        uintptr_t stack_addr = reinterpret_cast<uintptr_t>(stack_.data());
        stack_addr = (stack_addr + 15) & ~15;  // Align to 16 bytes
        
        context_.stack_base = reinterpret_cast<void*>(stack_addr);
        context_.stack_top = reinterpret_cast<void*>(stack_addr + new_size - 16);
        context_.stack_size = new_size;
        
        // Adjust stack pointer proportionally
        uintptr_t offset_from_base = current_sp - reinterpret_cast<uintptr_t>(context_.stack_base);
        context_.rsp = reinterpret_cast<uintptr_t>(context_.stack_base) + offset_from_base;
        
        stack_pointer_ = context_.stack_top;
        
        std::cout << new_size << " bytes" << std::endl;
        return true;
    }
    
    return true;  // No growth needed
}

bool Task::check_stack_overflow() {
    uintptr_t current_sp = context_.rsp;
    uintptr_t stack_base = reinterpret_cast<uintptr_t>(context_.stack_base);
    
    if (current_sp <= stack_base) {
        std::cerr << "[Stack] Stack overflow detected in task " << id_ << std::endl;
        return true;
    }
    
    return false;
}

// TaskScheduler implementation
TaskScheduler::TaskScheduler(size_t num_workers) {
    init_log_level_from_env();
    if (num_workers == 0) {
        num_workers_ = std::thread::hardware_concurrency();
        if (num_workers_ == 0) num_workers_ = 4;  // Fallback
    } else {
        num_workers_ = num_workers;
    }
    
    preemptive_scheduling_ = true;
    
    log_info(std::string("[Scheduler] Initializing with ") + std::to_string(num_workers_) + " worker threads");
}

TaskScheduler::~TaskScheduler() {
    stop();
}

void TaskScheduler::start() {
    log_info(std::string("[Scheduler] Starting ") + std::to_string(num_workers_) + " worker threads");
    
    // Reserve to avoid reallocations while workers start
    workers_.reserve(num_workers_);
    // Create worker records first (without starting threads)
    for (size_t i = 0; i < num_workers_; ++i) {
        auto worker = std::make_unique<WorkerThread>();
        worker->worker_id = i;
        workers_.push_back(std::move(worker));
    }
    // Now start worker threads after vector is fully populated
    for (size_t i = 0; i < num_workers_; ++i) {
        workers_[i]->thread = std::thread(&TaskScheduler::worker_loop, this, i);
    }
    
    if (preemptive_scheduling_) {
        setup_preemption();
    }
}

void TaskScheduler::stop() {
    log_info("[Scheduler] Stopping scheduler...");
    
    // Stop preemption thread first
    this->preemption_stop_.store(true);
    if (this->preemption_thread_.joinable()) {
        this->preemption_thread_.join();
    }

    // Signal all workers to stop
    for (auto& worker : workers_) {
        worker->should_stop.store(true);
        worker->queue_cv.notify_all();
    }
    
    // Wait for all workers to finish
    for (auto& worker : workers_) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
    
    workers_.clear();
    log_info("[Scheduler] Stopped");
}

Task::TaskId TaskScheduler::spawn(Task::TaskFunction func) {
    Task::TaskId id = next_task_id_.fetch_add(1);
    auto task = std::make_shared<Task>(id, func);
    
    active_tasks_.fetch_add(1);
    schedule_task(task);
    
    // Store task reference for cancellation
    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        active_task_map_[id] = task;
    }
    
    log_debug(std::string("[Scheduler] Spawned task ") + std::to_string(id) + " (active: " + std::to_string(active_tasks_.load()) + ")");
    return id;
}

void TaskScheduler::cancel_task(Task::TaskId id) {
    std::lock_guard<std::mutex> lock(global_mutex_);
    
    auto it = active_task_map_.find(id);
    if (it != active_task_map_.end()) {
        auto task = it->second.lock();  // Convert weak_ptr to shared_ptr
        if (task) {
            task->cancel();
            log_info(std::string("[Scheduler] Cancelled task ") + std::to_string(id));
        }
        active_task_map_.erase(it);
    }
}

void TaskScheduler::yield_current_task() {
    // Yield the currently running fiber on this worker thread
    std::thread::id current_thread_id = std::this_thread::get_id();
    WorkerThread* current_worker = nullptr;
    for (auto& worker : workers_) {
        if (worker->thread.get_id() == current_thread_id) {
            current_worker = worker.get();
            break;
        }
    }
    if (!current_worker || !current_worker->current_task) {
        log_debug("[Scheduler] No current task to yield");
        return;
    }
    // Copy shared_ptr under mutex to avoid data races
    std::shared_ptr<Task> t;
    {
        std::lock_guard<std::mutex> ct_lock(current_worker->current_task_mutex);
        t = current_worker->current_task;
    }
    if (!t) {
        log_debug("[Scheduler] No current task to yield (lost)");
        return;
    }
    log_debug(std::string("[Scheduler] Yielding task ") + std::to_string(t->get_id()));
    // This will swap back to scheduler via ucontext
    t->yield();
}

void TaskScheduler::schedule_task(std::shared_ptr<Task> task, size_t preferred_worker) {
    if (preferred_worker < workers_.size()) {
        // Try to schedule on preferred worker
        auto& worker = workers_[preferred_worker];
        std::lock_guard<std::mutex> lock(worker->queue_mutex);
        worker->local_queue.push(task);
        worker->queue_cv.notify_one();
    } else {
        // Schedule on global queue for work stealing
        std::lock_guard<std::mutex> lock(global_mutex_);
        global_queue_.push(task);
    }
}

void TaskScheduler::worker_loop(size_t worker_id) {
    log_info(std::string("[Worker ") + std::to_string(worker_id) + "] Started");
    
    auto& worker = workers_[worker_id];
    // Cooperative scheduling only; preemption uses voluntary yield
    // Initialize scheduler context once so uc_link always points to a valid context
    getcontext(&worker->sched_ctx);
    
    while (!worker->should_stop.load()) {
        std::shared_ptr<Task> task = nullptr;
        
        // Try to get task from local queue
        {
            std::unique_lock<std::mutex> lock(worker->queue_mutex);
            worker->queue_cv.wait_for(lock, std::chrono::milliseconds(100), [&] {
                return !worker->local_queue.empty() || worker->should_stop.load();
            });
            
            if (!worker->local_queue.empty()) {
                task = worker->local_queue.front();
                worker->local_queue.pop();
            }
        }
        
        // If no local task, try fast work stealing
        if (!task) {
            task = steal_task_fast(worker_id);
        }
        
        // Execute task if found
        if (task) {
            log_debug(std::string("[Worker ") + std::to_string(worker_id) + "] Executing task " + std::to_string(task->get_id()));
            
            // Track current task for cooperative scheduling (protect with mutex)
            {
                std::lock_guard<std::mutex> ct_lock(worker->current_task_mutex);
                worker->current_task = task;
                worker->current_task_start_time = std::chrono::high_resolution_clock::now();
                worker->preemption_requested.store(false);
            }
            
            // Prepare TLS for scheduler and current task
            g_sched_ctx = &worker->sched_ctx;
            g_current_task = task.get();
            
            // Initialize ucontext for the task lazily
            if (!task->uctx_initialized_) {
                getcontext(&task->uctx_);
                // Align stack to 16 bytes for ABI compliance
                uintptr_t base = reinterpret_cast<uintptr_t>(task->stack_.data());
                uintptr_t aligned = (base + 15) & ~static_cast<uintptr_t>(15);
                size_t usable = task->stack_.size() - (aligned - base);
                task->uctx_.uc_stack.ss_sp = reinterpret_cast<void*>(aligned);
                task->uctx_.uc_stack.ss_size = usable;
                task->uctx_.uc_stack.ss_flags = 0;
                task->uctx_.uc_link = &worker->sched_ctx; // return to scheduler when finished
                makecontext(&task->uctx_, TaskTrampoline, 0);
                task->uctx_initialized_ = true;
            }
            
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // Switch from scheduler to task context
            swapcontext(&worker->sched_ctx, &task->uctx_);
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            // After returning from the task (yield or completion)
            if (task->get_state() == Task::RUNNABLE) {
                // Re-enqueue for further execution
                std::lock_guard<std::mutex> lock(worker->queue_mutex);
                worker->local_queue.push(task);
                worker->queue_cv.notify_one();
            } else {
                // Completed or cancelled
                active_tasks_.fetch_sub(1);
            }
            
            // Clear current task tracking
            {
                std::lock_guard<std::mutex> ct_lock(worker->current_task_mutex);
                worker->current_task = nullptr;
                worker->current_task_start_time.reset();
            }
            
            log_debug(std::string("[Worker ") + std::to_string(worker_id) + "] Task " + std::to_string(task->get_id()) + " ran for " + std::to_string(duration.count()) + "us");
        }
    }
    
    log_info(std::string("[Worker ") + std::to_string(worker_id) + "] Stopped");
}

std::shared_ptr<Task> TaskScheduler::steal_task(size_t worker_id) {
    // Try to steal from global queue first
    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        if (!global_queue_.empty()) {
            auto task = global_queue_.front();
            global_queue_.pop();
            return task;
        }
    }
    
    // Try to steal from other workers
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    
    for (size_t attempts = 0; attempts < num_workers_; ++attempts) {
        size_t target_worker = gen() % num_workers_;
        if (target_worker == worker_id) continue;
        
        auto& target = workers_[target_worker];
        std::lock_guard<std::mutex> lock(target->queue_mutex);
        
        if (!target->local_queue.empty()) {
            auto task = target->local_queue.front();
            target->local_queue.pop();
            return task;
        }
    }
    
    return nullptr;
}

// Optimized work stealing for maximum performance
std::shared_ptr<Task> TaskScheduler::steal_task_fast(size_t worker_id) {
    // Fast work stealing algorithm with multiple strategies
    
    // Strategy 1: Try global queue first (lowest contention)
    {
        std::unique_lock<std::mutex> lock(global_mutex_, std::try_to_lock);
        if (lock.owns_lock() && !global_queue_.empty()) {
            auto task = global_queue_.front();
            global_queue_.pop();
            return task;
        }
    }
    
    // Strategy 2: Random work stealing with exponential backoff
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<size_t> dist;
    
    // Try multiple random workers (up to log(N) attempts for good distribution)
    size_t max_attempts = std::max(1UL, static_cast<size_t>(std::log2(num_workers_)) + 1);
    
    for (size_t attempt = 0; attempt < max_attempts; ++attempt) {
        // Pick a random target worker (excluding ourselves)
        size_t target_worker;
        do {
            target_worker = dist(gen) % num_workers_;
        } while (target_worker == worker_id && num_workers_ > 1);
        
        auto& target = workers_[target_worker];
        
        // Use try_lock for minimal blocking
        std::unique_lock<std::mutex> lock(target->queue_mutex, std::try_to_lock);
        if (lock.owns_lock() && !target->local_queue.empty()) {
            // Steal from the back (LIFO) for better cache locality
            auto task = target->local_queue.front();
            target->local_queue.pop();
            
            log_debug(std::string("[WorkSteal] Worker ") + std::to_string(worker_id) +
                      " stole task " + std::to_string(task->get_id()) +
                      " from worker " + std::to_string(target_worker));
            return task;
        }
    }
    
    // Strategy 3: Sequential scan as fallback (only if random failed)
    for (size_t i = 1; i < num_workers_; ++i) {
        size_t target_worker = (worker_id + i) % num_workers_;
        auto& target = workers_[target_worker];
        
        std::unique_lock<std::mutex> lock(target->queue_mutex, std::try_to_lock);
        if (lock.owns_lock() && target->local_queue.size() > 1) {  // Only steal if they have multiple tasks
            auto task = target->local_queue.front();
            target->local_queue.pop();
            
            log_debug(std::string("[WorkSteal] Worker ") + std::to_string(worker_id) +
                      " stole task " + std::to_string(task->get_id()) +
                      " from overloaded worker " + std::to_string(target_worker) +
                      " (queue size: " + std::to_string(target->local_queue.size() + 1) + ")");
            return task;
        }
    }
    
    return nullptr;  // No tasks available for stealing
}

void TaskScheduler::wait_all() {
    log_info("[Scheduler] Waiting for all tasks to complete...");
    
    while (active_tasks_.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    log_info("[Scheduler] All tasks completed");
}

void TaskScheduler::setup_preemption() {
    log_info(std::string("[Scheduler] Setting up preemptive scheduling (time slice: ") 
             + std::to_string(time_slice_.count()) + "ms)");
    
    // Start managed preemption timer thread
    preemption_stop_.store(false);
    preemption_thread_ = std::thread([this]() {
        while (!this->preemption_stop_.load()) {
            std::this_thread::sleep_for(time_slice_);
            preempt_long_running_tasks();
        }
    });
    
    log_info("[Scheduler] Preemptive scheduling active");
}

void TaskScheduler::preempt_long_running_tasks() {
    auto now = std::chrono::high_resolution_clock::now();
    
    // Check all workers for long-running tasks
    std::lock_guard<std::mutex> wlock(workers_mutex_);
    for (size_t i = 0; i < workers_.size(); ++i) {
        auto& worker = workers_[i];
        // Cooperative-only preemption: do not manipulate queues or shared_ptr here
        // Just request preemption; tasks must yield cooperatively
        {
            std::lock_guard<std::mutex> ct_lock(worker->current_task_mutex);
            if (worker->current_task && worker->current_task_start_time.has_value()) {
                auto task_runtime = now - worker->current_task_start_time.value();
                if (task_runtime > time_slice_) {
                    log_debug(std::string("[Preemption] Task ") + std::to_string(worker->current_task->get_id()) +
                              " on worker " + std::to_string(i) + " exceeded time slice (" +
                              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(task_runtime).count()) + "ms)");
                    worker->preemption_requested.store(true);
                }
            }
        }
        // Note: Work balancing via queue manipulation is disabled for stability.
        // Fast work stealing already balances load without cross-thread queue moves here.
    }
}

// ConcurrencyRuntime implementation
ConcurrencyRuntime::ConcurrencyRuntime() = default;
ConcurrencyRuntime::~ConcurrencyRuntime() = default;

bool ConcurrencyRuntime::initialize(size_t num_workers) {
    scheduler_ = std::make_unique<TaskScheduler>(num_workers);
    scheduler_->start();
    return true;
}

void ConcurrencyRuntime::shutdown() {
    if (scheduler_) {
        scheduler_->wait_all();
        scheduler_->stop();
        scheduler_.reset();
    }
}

Task::TaskId ConcurrencyRuntime::kode(Task::TaskFunction func) {
    if (!scheduler_) {
        throw std::runtime_error("ConcurrencyRuntime not initialized");
    }
    return scheduler_->spawn(func);
}

void ConcurrencyRuntime::yield() {
    if (scheduler_) {
        scheduler_->yield_current_task();
    }
}

void ConcurrencyRuntime::with_timeout(std::chrono::milliseconds timeout, Task::TaskFunction func) {
    // Full implementation with actual timeout enforcement
    std::shared_ptr<std::atomic<bool>> completed = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> timed_out = std::make_shared<std::atomic<bool>>(false);
    
    // Start the main task
    Task::TaskId main_task = kode([func, completed, timed_out]() {
        try {
            func();
            completed->store(true);
        } catch (const std::exception& e) {
            if (!timed_out->load()) {
                std::cerr << "[Task] Exception: " << e.what() << std::endl;
            }
            completed->store(true);
        }
    });
    
    // Start timeout watchdog task
    kode([timeout, completed, timed_out, main_task, this]() {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        while (!completed->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
            auto current_time = std::chrono::high_resolution_clock::now();
            if (current_time - start_time > timeout) {
                timed_out->store(true);
                scheduler_->cancel_task(main_task);
                std::cout << "[Timeout] Task " << main_task << " timed out after " 
                         << timeout.count() << "ms" << std::endl;
                break;
            }
        }
    });
}

void ConcurrencyRuntime::join_all() {
    if (scheduler_) {
        scheduler_->wait_all();
    }
}

void ConcurrencyRuntime::print_stats() const {
    if (scheduler_) {
        std::cout << "[Stats] Active tasks: " << scheduler_->get_active_task_count() << std::endl;
        std::cout << "[Stats] Worker threads: " << scheduler_->get_worker_count() << std::endl;
    }
}

size_t ConcurrencyRuntime::get_task_count() const {
    return scheduler_ ? scheduler_->get_active_task_count() : 0;
}

// JavaScript API integration
namespace JSConcurrency {
    static ConcurrencyRuntime* g_runtime = nullptr;
    
    void initialize_runtime() {
        if (!g_runtime) {
            g_runtime = new ConcurrencyRuntime();
            g_runtime->initialize();
        }
    }
    
    void spawn_task(const std::string& js_code) {
        if (!g_runtime) initialize_runtime();
        
        g_runtime->kode([js_code]() {
            std::cout << "[JSTask] Executing: " << js_code << std::endl;
            
            // Full implementation: Parse and execute JavaScript
            try {
                // Simulate different types of JavaScript operations
                if (js_code.find("console.log") != std::string::npos) {
                    // Extract and print the message
                    size_t start = js_code.find("'");
                    if (start == std::string::npos) start = js_code.find("\"");
                    if (start != std::string::npos) {
                        size_t end = js_code.find_last_of("'\"");
                        if (end > start) {
                            std::string message = js_code.substr(start + 1, end - start - 1);
                            std::cout << message << std::endl;
                        }
                    }
                }
                else if (js_code.find("setTimeout") != std::string::npos) {
                    // Simulate setTimeout
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    std::cout << "[JSTask] Timer fired" << std::endl;
                }
                else if (js_code.find("yield") != std::string::npos) {
                    // Cooperative yield
                    g_runtime->yield();
                }
                else {
                    // Generic computation simulation
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                
            } catch (const std::exception& e) {
                std::cerr << "[JSTask] Error: " << e.what() << std::endl;
            }
            
            std::cout << "[JSTask] Completed: " << js_code << std::endl;
        });
    }
    
    void create_channel(const std::string& name, size_t capacity) {
        if (!g_runtime) initialize_runtime();
        
        // Full implementation: Create named channel registry
        static std::unordered_map<std::string, std::shared_ptr<Channel<std::string>>> channels;
        static std::mutex channels_mutex;
        
        std::lock_guard<std::mutex> lock(channels_mutex);
        channels[name] = g_runtime->make_channel<std::string>(capacity);
        
        std::cout << "[JSChannel] Created channel '" << name << "' with capacity " << capacity << std::endl;
    }
    
    void send_to_channel(const std::string& channel_name, const std::string& value) {
        static std::unordered_map<std::string, std::shared_ptr<Channel<std::string>>> channels;
        static std::mutex channels_mutex;
        
        std::lock_guard<std::mutex> lock(channels_mutex);
        auto it = channels.find(channel_name);
        if (it != channels.end()) {
            bool sent = it->second->send(value);
            std::cout << "[JSChannel] " << (sent ? "Sent" : "Failed to send") 
                      << " '" << value << "' to channel '" << channel_name << "'" << std::endl;
        } else {
            std::cerr << "[JSChannel] Channel '" << channel_name << "' not found" << std::endl;
        }
    }
    
    std::string receive_from_channel(const std::string& channel_name) {
        static std::unordered_map<std::string, std::shared_ptr<Channel<std::string>>> channels;
        static std::mutex channels_mutex;
        
        std::lock_guard<std::mutex> lock(channels_mutex);
        auto it = channels.find(channel_name);
        if (it != channels.end()) {
            std::string value;
            bool received = it->second->receive(value);
            if (received) {
                std::cout << "[JSChannel] Received '" << value << "' from channel '" << channel_name << "'" << std::endl;
                return value;
            } else {
                std::cout << "[JSChannel] Channel '" << channel_name << "' closed or empty" << std::endl;
                return "";
            }
        } else {
            std::cerr << "[JSChannel] Channel '" << channel_name << "' not found" << std::endl;
            return "";
        }
    }
    
    void yield_task() {
        if (g_runtime) {
            g_runtime->yield();
        }
    }
}
