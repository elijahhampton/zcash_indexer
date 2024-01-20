#ifndef THREAD_POOL
#define THREAD_POOL

#include <boost/asio/io_service.hpp>
#include <boost/thread/thread.hpp>
#include <functional>
#include <iostream>
#include <mutex>
#include <condition_variable> 
#include <atomic>

class ThreadPool
{
private:
    std::mutex cs_task_mutex;
    std::condition_variable cv_task;
    boost::asio::io_service io_service;
    boost::thread_group worker_threads;
    std::unique_ptr<boost::asio::io_service::work> work;
    std::atomic<uint8_t> active_task{0};

    void InitNewWork();
public:
    static const uint8_t MAX_HARDWARE_THREADS;

    ThreadPool();
    ThreadPool operator=(const ThreadPool &pool) = delete;
    ThreadPool(const ThreadPool &pool) = delete;
    ~ThreadPool();

    void RefreshThreadPool();
    void TaskCompleted();
    void End();

    template <typename F, typename... Args>
    void SubmitTask(F &&f, Args &&...args)
    {
        std::unique_lock<std::mutex> cs_task_lock(cs_task_mutex);
        cv_task.wait(cs_task_lock, [this]{ 
            return static_cast<int>(this->active_task) < static_cast<int>(ThreadPool::MAX_HARDWARE_THREADS);
            });

        this->io_service.post(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        ++this->active_task;
    }


};

#endif // THREAD_POOL
