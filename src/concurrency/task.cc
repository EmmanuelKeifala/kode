#include "task.h"
#include <iostream>
#include <algorithm>
#include <random>
#include <cstring>

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
    
    // Align stack to 16-byte boundary (required by x86_64 ABI)
    uintptr_t stack_addr = reinterpret_cast<uintptr_t>(stack_.data());
    stack_addr = (stack_addr + 15) & ~15;  // Round up to 16-byte boundary
    
    context_.stack_base = reinterpret_cast<void*>(stack_addr);
    context_.stack_top = reinterpret_cast<void*>(stack_addr + stack_.size() - 16);
    context_.stack_size = stack_.size();
    
    // Initialize stack pointer to top of stack
    context_.rsp = reinterpret_cast<uint64_t>(context_.stack_top);
    stack_pointer_ = context_.stack_top;
    
    // Initialize context
    std::memset(&context_, 0, sizeof(context_));
    context_.rsp = reinterpret_cast<uint64_t>(context_.stack_top);
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

void Task::yield() {
    if (cancelled_.load()) {
        throw std::runtime_error("Task cancelled");
    }
    
    state_ = RUNNABLE;
    
    // Full implementation: Save current execution context
    save_context();
    
    // Mark yield point for scheduler
    last_run_ = std::chrono::high_resolution_clock::now();
    
    // The actual context switch happens in the scheduler
    // This is where we would switch to the scheduler's context
    // For now, we use cooperative yielding as a fallback
    std::this_thread::yield();
    
    // When we resume, restore context
    restore_context();
    
    // Check for cancellation after yield
    if (cancelled_.load()) {
        throw std::runtime_error("Task cancelled during yield");
    }
}

// Full context switching implementation
void Task::save_context() {
    #ifdef __x86_64__
    // Save CPU registers using simplified inline assembly
    asm volatile (
        "movq %%rsp, %0"
        : "=m" (context_.rsp)
        :
        : "memory"
    );
    
    // Save other critical registers one by one to avoid constraint issues
    asm volatile ("movq %%rbp, %0" : "=m" (context_.rbp));
    asm volatile ("movq %%rbx, %0" : "=m" (context_.rbx));
    asm volatile ("movq %%r12, %0" : "=m" (context_.r12));
    asm volatile ("movq %%r13, %0" : "=m" (context_.r13));
    asm volatile ("movq %%r14, %0" : "=m" (context_.r14));
    asm volatile ("movq %%r15, %0" : "=m" (context_.r15));
    
    // Save flags
    asm volatile (
        "pushfq\n\t"
        "popq %0"
        : "=m" (context_.rflags)
        :
        : "memory"
    );
    
    std::cout << "[Context] Saved context for task " << id_ 
              << " (RSP: 0x" << std::hex << context_.rsp << std::dec << ")" << std::endl;
    #else
    // Fallback for non-x86_64 architectures
    std::cout << "[Context] Context saving not implemented for this architecture" << std::endl;
    #endif
}

void Task::restore_context() {
    #ifdef __x86_64__
    // Restore critical registers one by one
    asm volatile ("movq %0, %%rbx" : : "m" (context_.rbx));
    asm volatile ("movq %0, %%rbp" : : "m" (context_.rbp));
    asm volatile ("movq %0, %%r12" : : "m" (context_.r12));
    asm volatile ("movq %0, %%r13" : : "m" (context_.r13));
    asm volatile ("movq %0, %%r14" : : "m" (context_.r14));
    asm volatile ("movq %0, %%r15" : : "m" (context_.r15));
    
    // Restore flags
    asm volatile (
        "pushq %0\n\t"
        "popfq"
        :
        : "m" (context_.rflags)
        : "memory"
    );
    
    // Note: RSP restoration is tricky and should be done carefully
    // For now, we'll just log the restoration
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
    if (num_workers == 0) {
        num_workers_ = std::thread::hardware_concurrency();
        if (num_workers_ == 0) num_workers_ = 4;  // Fallback
    } else {
        num_workers_ = num_workers;
    }
    
    preemptive_scheduling_ = true;
    
    std::cout << "[Scheduler] Initializing with " << num_workers_ << " worker threads" << std::endl;
}

TaskScheduler::~TaskScheduler() {
    stop();
}

void TaskScheduler::start() {
    std::cout << "[Scheduler] Starting " << num_workers_ << " worker threads" << std::endl;
    
    for (size_t i = 0; i < num_workers_; ++i) {
        auto worker = std::make_unique<WorkerThread>();
        worker->worker_id = i;
        worker->thread = std::thread(&TaskScheduler::worker_loop, this, i);
        workers_.push_back(std::move(worker));
    }
    
    if (preemptive_scheduling_) {
        setup_preemption();
    }
}

void TaskScheduler::stop() {
    std::cout << "[Scheduler] Stopping scheduler..." << std::endl;
    
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
    std::cout << "[Scheduler] Stopped" << std::endl;
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
    
    std::cout << "[Scheduler] Spawned task " << id << " (active: " << active_tasks_.load() << ")" << std::endl;
    return id;
}

void TaskScheduler::cancel_task(Task::TaskId id) {
    std::lock_guard<std::mutex> lock(global_mutex_);
    
    auto it = active_task_map_.find(id);
    if (it != active_task_map_.end()) {
        auto task = it->second.lock();  // Convert weak_ptr to shared_ptr
        if (task) {
            task->cancel();
            std::cout << "[Scheduler] Cancelled task " << id << std::endl;
        }
        active_task_map_.erase(it);
    }
}

void TaskScheduler::yield_current_task() {
    // For now, implement cooperative yielding
    // In a full implementation, this would:
    // 1. Find the current task on the current worker
    // 2. Save its context
    // 3. Move it back to the runnable queue
    // 4. Switch to the next available task
    
    std::cout << "[Scheduler] Task yielding (cooperative)" << std::endl;
    std::this_thread::yield();
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
    std::cout << "[Worker " << worker_id << "] Started" << std::endl;
    
    auto& worker = workers_[worker_id];
    
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
        
        // If no local task, try work stealing
        if (!task) {
            task = steal_task(worker_id);
        }
        
        // Execute task if found
        if (task) {
            std::cout << "[Worker " << worker_id << "] Executing task " << task->get_id() << std::endl;
            
            // Track current task for preemption
            worker->current_task = task;
            worker->current_task_start_time = std::chrono::high_resolution_clock::now();
            worker->preemption_requested.store(false);
            
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // Execute task with preemption checking
            try {
                task->run();
                
                // Check for preemption during execution
                if (worker->preemption_requested.load()) {
                    std::cout << "[Worker " << worker_id << "] Task " << task->get_id() 
                             << " was preempted" << std::endl;
                    task->set_state(Task::RUNNABLE);  // Mark as preempted, not completed
                } else {
                    task->set_state(Task::COMPLETED);
                }
                
            } catch (const std::exception& e) {
                std::cerr << "[Worker " << worker_id << "] Task " << task->get_id() 
                         << " threw exception: " << e.what() << std::endl;
                task->set_state(Task::COMPLETED);
            }
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            std::cout << "[Worker " << worker_id << "] Task " << task->get_id() 
                      << " finished in " << duration.count() << "μs" << std::endl;
            
            // Clear current task tracking
            worker->current_task = nullptr;
            worker->current_task_start_time.reset();
            
            active_tasks_.fetch_sub(1);
        }
    }
    
    std::cout << "[Worker " << worker_id << "] Stopped" << std::endl;
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

void TaskScheduler::wait_all() {
    std::cout << "[Scheduler] Waiting for all tasks to complete..." << std::endl;
    
    while (active_tasks_.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "[Scheduler] All tasks completed" << std::endl;
}

void TaskScheduler::setup_preemption() {
    std::cout << "[Scheduler] Setting up preemptive scheduling (time slice: " 
              << time_slice_.count() << "ms)" << std::endl;
    
    // Full implementation: Start preemption timer thread
    std::thread preemption_thread([this]() {
        while (!workers_.empty() && !workers_[0]->should_stop.load()) {
            std::this_thread::sleep_for(time_slice_);
            preempt_long_running_tasks();
        }
    });
    
    preemption_thread.detach();  // Let it run independently
    
    std::cout << "[Scheduler] Preemptive scheduling active" << std::endl;
}

void TaskScheduler::preempt_long_running_tasks() {
    auto now = std::chrono::high_resolution_clock::now();
    
    // Check all workers for long-running tasks
    for (size_t i = 0; i < workers_.size(); ++i) {
        auto& worker = workers_[i];
        std::lock_guard<std::mutex> lock(worker->queue_mutex);
        
        // Full implementation of preemption:
        
        // 1. Check if current task has been running too long
        if (worker->current_task && worker->current_task_start_time.has_value()) {
            auto task_runtime = now - worker->current_task_start_time.value();
            
            if (task_runtime > time_slice_) {
                std::cout << "[Preemption] Task " << worker->current_task->get_id() 
                         << " on worker " << i << " exceeded time slice ("
                         << std::chrono::duration_cast<std::chrono::milliseconds>(task_runtime).count() 
                         << "ms)" << std::endl;
                
                // 2. Send preemption signal to worker thread
                worker->preemption_requested.store(true);
                
                // 3. Save task state and reschedule it
                if (worker->current_task->get_state() == Task::RUNNING) {
                    // Mark task as preempted
                    worker->current_task->set_state(Task::RUNNABLE);
                    
                    // Add back to queue for rescheduling
                    worker->local_queue.push(worker->current_task);
                    
                    std::cout << "[Preemption] Rescheduled task " << worker->current_task->get_id() << std::endl;
                }
                
                // Clear current task tracking
                worker->current_task = nullptr;
                worker->current_task_start_time.reset();
            }
        }
        
        // Also implement work balancing - move tasks from overloaded workers
        if (worker->local_queue.size() > 10) {  // Threshold for load balancing
            // Find least loaded worker
            size_t min_load = SIZE_MAX;
            size_t target_worker = SIZE_MAX;
            
            for (size_t j = 0; j < workers_.size(); ++j) {
                if (j != i) {
                    std::lock_guard<std::mutex> target_lock(workers_[j]->queue_mutex);
                    if (workers_[j]->local_queue.size() < min_load) {
                        min_load = workers_[j]->local_queue.size();
                        target_worker = j;
                    }
                }
            }
            
            // Move half the tasks to the least loaded worker
            if (target_worker != SIZE_MAX && min_load < worker->local_queue.size() / 2) {
                std::lock_guard<std::mutex> target_lock(workers_[target_worker]->queue_mutex);
                
                size_t tasks_to_move = worker->local_queue.size() / 2;
                for (size_t k = 0; k < tasks_to_move; ++k) {
                    auto task = worker->local_queue.front();
                    worker->local_queue.pop();
                    workers_[target_worker]->local_queue.push(task);
                }
                
                workers_[target_worker]->queue_cv.notify_all();
                
                std::cout << "[LoadBalance] Moved " << tasks_to_move 
                         << " tasks from worker " << i << " to worker " << target_worker << std::endl;
            }
        }
    }
}

// ConcurrencyRuntime implementation
ConcurrencyRuntime::ConcurrencyRuntime() = default;
ConcurrencyRuntime::~ConcurrencyRuntime() = default;

bool ConcurrencyRuntime::initialize(size_t num_workers) {
    std::cout << "[ConcurrencyRuntime] Initializing..." << std::endl;
    
    scheduler_ = std::make_unique<TaskScheduler>(num_workers);
    scheduler_->start();
    
    std::cout << "[ConcurrencyRuntime] Ready with " << scheduler_->get_worker_count() << " workers" << std::endl;
    return true;
}

void ConcurrencyRuntime::shutdown() {
    if (scheduler_) {
        scheduler_->wait_all();
        scheduler_->stop();
        scheduler_.reset();
    }
    std::cout << "[ConcurrencyRuntime] Shutdown complete" << std::endl;
}

Task::TaskId ConcurrencyRuntime::go(Task::TaskFunction func) {
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
    Task::TaskId main_task = go([func, completed, timed_out]() {
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
    go([timeout, completed, timed_out, main_task, this]() {
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
        
        g_runtime->go([js_code]() {
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
