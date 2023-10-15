#include "controller.h"
#include "json/json.h"

#include <vector>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <mutex>

//YAML::Node config = YAML::LoadFile("config.yaml");

Controller::Controller(): rpcClient("http://127.0.0.1:8232", "elijah", "Hamptonej1!"), syncer(rpcClient, database)
{
    std::string connection_string = this->database.LoadConfig("./src/database.cfg");
    if (!this->database.Connect(20, connection_string))
    {
        throw std::runtime_error("Database failed to open.");
    }
}

Controller::~Controller()
{
    this->Shutdown();
}

void Controller::Init()
{
    if (!this->database.CreateTables()) {
        throw std::runtime_error("Database failed to create tables.");
    }
}

void Controller::StartSyncLoop() {

    std::mutex syncMutex;
    const std::chrono::seconds syncInterval(60); 

    while (true) {
        std::lock_guard<std::mutex> syncLock(syncMutex); 
        
        bool shouldSync = this->syncer.ShouldSyncWallet();

        if (shouldSync)
        {
            std::thread syncThread(&Controller::StartSync, this);
            syncThread.detach();
        }

        std::cout << "Sleeping before checking sync thread: " << syncInterval.count() << " seconds" << std::endl;
        std::this_thread::sleep_for(syncInterval);
    }

}

void Controller::StartSync() {
    this->syncer.Sync();
}

void Controller::Shutdown()
{

}

int main()
{
    Controller controller;
    controller.Init();
    controller.StartSyncLoop();
    return 0;
}