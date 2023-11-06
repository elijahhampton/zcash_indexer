#include "controller.h"
//#include "json/json.h"

#include <vector>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <mutex>
#include "config.h"

Controller::Controller()
try : rpcClient(Config::getRpcUrl(), Config::getRpcUsername(), Config::getRpcPassword()), syncer(rpcClient, database)
{
    const std::string connection_string =
        "dbname=" + Config::getDatabaseName() +
        " user=" + Config::getDatabaseUser() +
        " password=" + Config::getDatabasePassword() +
        " host=" + Config::getDatabaseHost() +
        " port=" + Config::getDatabasePort();

        std::cout << connection_string << std::endl;
    // Five connections are assigned to each hardware thread
    unsigned int poolSize = std::thread::hardware_concurrency() * 5;
    if (!this->database.Connect(poolSize, connection_string))
    {
        throw std::runtime_error("Database failed to open.");
    }
}
catch (const std::exception& e)
{
    std::cerr << "Error: " << e.what() << std::endl;
    throw;
}

Controller::~Controller()
{
    this->Shutdown();
}

void Controller::InitAndSetup()
{
    if (!this->database.CreateTables())
    {
        throw std::runtime_error("Database failed to create tables.");
    }
}

void Controller::StartSyncLoop()
{

    std::mutex syncMutex;
    const std::chrono::seconds syncInterval(60);

    while (true)
    {
        std::lock_guard<std::mutex> syncLock(syncMutex);

        bool shouldSync = this->syncer.ShouldSyncWallet();

        if (shouldSync)
        {
            std::thread syncThread(&Controller::StartSync, this);
            syncThread.detach();
        }

        std::cout << "Sleeping before checking sync thread: " << syncInterval.count() << " seconds" << std::flush;
        std::this_thread::sleep_for(syncInterval);
    }
}

void Controller::StartSync()
{
    this->syncer.Sync();
}

void Controller::Shutdown()
{
}

int main()
{
    Controller controller;
    controller.InitAndSetup();
    controller.StartSyncLoop();
    return 0;
}