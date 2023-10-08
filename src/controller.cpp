#include "controller.h"
#include "json/json.h"

#include <vector>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <mutex>
#include <yaml-cpp/yaml.h>

YAML::Node config = YAML::LoadFile("config.yaml");

Controller::Controller(): rpcClient(config["rpc"]["host"], config["rpc"]["username"], config["rpc"]["password"]), syncer(rpcClient, database)
{
    std::string connStr = std::format("dbname={} user={} password={} hostaddr={} port={}", config["database"]["dbname"], config["database"]["username"], config["database"]["password"], config["database"]["hostaddr"], config["database"]["port"]);
    if (!this->database.Connect(20, connStr))
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