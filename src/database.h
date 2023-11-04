#include <memory>
#include <pqxx/pqxx>
#include <queue>
#include "json/json.h"
#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <stack>
#include <condition_variable>
#include <exception>
#include <stdexcept>
#include <map>
#include <thread>
#include <fstream>

#include <vector>
#include <queue>

class Database {
private:
    std::queue<std::unique_ptr<pqxx::connection>> connectionPool;
    std::mutex poolMutex;
    std::condition_variable poolCondition;

    void ShutdownConnections();
    void ReleaseConnection(std::unique_ptr<pqxx::connection> conn);
    std::unique_ptr<pqxx::connection> GetConnection();
    void UpdateChunkCheckpoint(size_t chunkStartHeight, size_t checkpointUpdateValue);
    bool StoreTransactions(const Json::Value& block, const std::unique_ptr<pqxx::connection>& conn, pqxx::work &blockTransaction);
    void AddMissedBlock(size_t blockHeight);
    void RemoveMissedBlock(size_t blockHeight);

public:

    // TODO: Update variable name to better reflect purpose. chunkStartHeight -> rangeCheckpointStart
    struct Checkpoint {
        size_t chunkStartHeight;
        size_t chunkEndHeight;
        size_t lastCheckpoint;
    };

    static std::mutex databaseConnectionCloseMutex;
    static std::condition_variable databaseConnectionCloseCondition;

    Database();
    ~Database();
    bool CreateTables();
    bool Connect(size_t poolSize, const std::string& connection_string);
    void StoreChunk(bool isTrackingCheckpointForChunk, const std::vector<Json::Value> &chunk, signed int chunkStartHeight, signed int chunkEndHeight, signed int lastCheckpoint = -1, signed int trueRangeStartHeight = -1);
    void LoadAndProcessUnprocessedChunks();
    std::stack<Database::Checkpoint> GetUnfinishedCheckpoints();
    std::optional<Database::Checkpoint> GetCheckpoint(signed int chunkStartHeight);
    unsigned int GetSyncedBlockCountFromDB();
    void CreateCheckpointIfNonExistent(size_t chunkStartHeight, size_t chunkEndHeight);
};
