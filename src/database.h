#include <memory>
#include <pqxx/pqxx>
#include <queue>
#include <json.h>
#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <stdexcept>
#include <thread>
#include <fstream>
#include <vector>
#include "block.h"

class Database {
private:
    std::queue<std::unique_ptr<pqxx::connection>> connectionPool;
    std::mutex poolMutex;
    std::mutex db_mutex;
    
public:
    static std::mutex databaseConnectionCloseMutex;
    static std::condition_variable databaseConnectionCloseCondition;

    Database();
    ~Database();
    bool CreateTables();
    bool Connect(size_t poolSize, std::string);
    void StoreBlock(const Json::Value& block);
    void StoreTransactions(const Json::Value& block);
    void StoreBlockHeader(const Json::Value& block);
    void StoreChunk(const std::vector<Json::Value> &chunk);
    unsigned int GetSyncedBlockCountFromDB();
    std::string LoadConfig(const std::string &path);
private:
    void ShutdownConnections();
    bool ReleaseConnection(std::unique_ptr<pqxx::connection> conn);
    std::unique_ptr<pqxx::connection> GetConnection();

};
