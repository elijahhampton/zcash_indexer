#include "thread_pool.h"
#include <boost/bind/bind.hpp>
#include <thread>

const uint8_t ThreadPool::MAX_HARDWARE_THREADS = std::thread::hardware_concurrency();

ThreadPool::ThreadPool() : work(new boost::asio::io_service::work(this->io_service)), active_task(0)
{
    for (size_t i = 0; i < ThreadPool::MAX_HARDWARE_THREADS; ++i)
    {
        this->worker_threads.create_thread(
            boost::bind(&boost::asio::io_service::run, &this->io_service));
    }
}

void ThreadPool::TaskCompleted()
{
    std::lock_guard<std::mutex> cs_task_lock(cs_task_mutex);
    --this->active_task;
    cv_task.notify_one();
}

void ThreadPool::Restart()
{
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
    this->worker_threads.join_all();
}

ThreadPool::~ThreadPool()
{
    this->End();
}
