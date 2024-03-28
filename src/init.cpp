#include "controllers/controller.h"
#include "sync/syncer.h"
#include "database/database.h"
#include "spdlog/spdlog.h"
#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <memory>

int InitApp() {
    // Configure spd logger
    spdlog::set_level(spdlog::level::debug);

    // Read YAML containing the application configuration via ENV
    YAML::Node config = YAML::LoadFile(std::getenv("CONFIG_PATH"));

    // Setup app components
    auto database = std::make_unique<Database>();
    auto rpcClient = std::make_unique<CustomClient>(config["rpc"]["url"].as<std::string>(), config["rpc"]["username"].as<std::string>(), config["rpc"]["password"].as<std::string>());
    auto syncer = std::make_unique<Syncer>(*rpcClient, *database, config["configuration"]["block_chunk_processing_size"].as<int>());

    // Connect Database
    const std::string connection_string =
        "dbname=" + config["database"]["dbname"].as<std::string>() +
        " user=" + config["database"]["host"].as<std::string>() +
        " password=" + config["database"]["password"].as<std::string>() +
        " host=" + config["database"]["host"].as<std::string>() +
        " port=" + config["database"]["port"].as<std::string>();

    spdlog::debug(connection_string.c_str());

    size_t poolSize = std::thread::hardware_concurrency() * 5;
    spdlog::debug(("Initializing database with pool size: " + std::to_string(poolSize)).c_str());
    Database::Connect(poolSize, connection_string);

    Controller controller(std::move(rpcClient), std::move(syncer), std::move(database));
    controller.InitAndSetup();
    controller.StartSyncLoop();
    controller.StartMonitoringPeers();
    controller.StartMonitoringChainInfo();
    controller.JoinJoinableSyncingOperations();
    controller.Shutdown();

    return 0;
}

int main() {
    int app_success = InitApp();
    return app_success;
}