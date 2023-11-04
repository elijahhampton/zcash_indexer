#include "controller.h"
#include "json/json.h"

#include <vector>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <mutex>

Controller::Controller(const InitConfig &config)
try : rpcClient(config.rpc_url, config.rpc_username, config.rpc_password), syncer(rpcClient, database)
{
    const std::string connection_string =
        "dbname=" + config.db_name +
        " user=" + config.db_user +
        " password=" + config.db_password +
        " host=" + config.db_host +
        " port=" + std::to_string(config.db_port);

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

int main(int argc, char* argv[])
{
    InitConfig initConfig;

    // Check the number of arguments injected
    if (argc < 8)
    {
        throw std::runtime_error("Invalid number of arguments. Arguments must include: --dbname, --dbpassword, --dbuser, --dbhost, --dbport, --rpcusername, --rpcpassword, --rpcurl");
    }

    //  Check for the required arguments and define the InitConfig fields
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];

        if (arg == "--dbname" && i + 1 < argc)
        {
            initConfig.db_name = argv[++i];
        }
        else if (arg == "--dbpassword" && i + 1 < argc)
        {
            initConfig.db_password = argv[++i];
        }
        else if (arg == "--dbuser" && i + 1 < argc)
        {
            initConfig.db_user = argv[++i];
        }
        else if (arg == "--dbhost" && i + 1 < argc)
        {
            initConfig.db_host = argv[++i];
        }
        else if (arg == "--dbport" && i + 1 < argc)
        {
            initConfig.db_port = std::stoi(argv[++i]);
        }
        else if (arg == "--rpcusername" && i + 1 < argc)
        {
            initConfig.rpc_username = argv[++i];
        }
        else if (arg == "--rpcpassword" && i + 1 < argc)
        {
            initConfig.rpc_password = argv[++i];
        }
        else if (arg == "--rpcurl" && i + 1 < argc)
        {
            initConfig.rpc_url = argv[++i];
        }
        else
        {
            std::cerr << "Unknown or improperly used argument: " << arg << std::endl;
            return 1;
        }
    }

    Controller controller(initConfig);
    controller.InitAndSetup();
    controller.StartSyncLoop();
    return 0;
}