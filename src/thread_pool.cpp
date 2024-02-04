#include "thread_pool.h"
#include <boost/bind/bind.hpp>
#include <thread>

const uint8_t ThreadPool::MAX_HARDWARE_THREADS = std::thread::hardware_concurrency() - 2;

ThreadPool::ThreadPool() : work(new boost::asio::io_service::work(this->io_service)), active_task(0)
{
    __DEBUG__(("Creating " + std::to_string(ThreadPool::MAX_HARDWARE_THREADS) + " worker threads.").c_str());
    for (size_t i = 0; i < ThreadPool::MAX_HARDWARE_THREADS; ++i)
    {
        this->worker_threads.create_thread(
            boost::bind(&boost::asio::io_service::run, &this->io_service));
    }
}

void ThreadPool::TaskCompleted()
{
    __INFO__("ThreadPool::TaskCompleted");
    std::lock_guard<std::mutex> cs_task_lock(cs_task_mutex);
    --this->active_task;
    cv_task.notify_one();
}

void ThreadPool::RefreshThreadPool()
{
    __INFO__(("Refreshing thread pool. Active task=: " + std::to_string(this->active_task)).c_str());

    if (this->active_task > 0) {
        this->worker_threads.join_all();
    }


    if (!this->io_service.stopped())
    {
        this->End();
    }

    this->InitNewWork();

    for (size_t i = 0; i < ThreadPool::MAX_HARDWARE_THREADS; ++i)
    {
        this->worker_threads.create_thread(
            boost::bind(&boost::asio::io_service::run, &this->io_service));
    }
}

void ThreadPool::InitNewWork()
{
    __INFO__("Resetting IO Service and creating new work.");
    if (this->io_service.stopped())
    {
        this->io_service.reset();
    }

    this->work = std::make_unique<boost::asio::io_service::work>(this->io_service);
}

void ThreadPool::End()
{
    this->work.reset();
    this->io_service.stop();
}

bool ThreadPool::isEmpty() {
    return this->active_task == 0;
}

ThreadPool::~ThreadPool()
{
    this->End();
}
