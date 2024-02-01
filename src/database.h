#include <memory>
#include <pqxx/pqxx>
#include <queue>
#include <sstream>
#include <mutex>
#include <stack>
#include <condition_variable>
#include <exception>
#include <stdexcept>
#include <map>
#include <thread>
#include <chrono>
#include <fstream>
#include <queue>
#include <limits>
#include <jsonrpccpp/common/jsonparser.h>

#include "httpclient.h"
#include "logger.h"
#include "controller.h"

#ifndef DATABASE_H
#define DATABASE_H

class Database {

friend class Controller;
friend class Syncer;

private:
    std::queue<std::unique_ptr<pqxx::connection>> mutable connection_pool;
    std::mutex mutable cs_connection_pool;
    std::condition_variable mutable cv_connection_pool;

    bool is_connected{false};
    bool is_database_setup{false};

/**
     * Shuts down all connections in the connection pool.
     * Closes each connection and clears the pool.
     */
    void ShutdownConnections() const;

    /**
     * Releases a database connection back to the connection pool.
     *
     * @param conn A unique pointer to the pqxx connection to be released.
     */
    void ReleaseConnection(std::unique_ptr<pqxx::connection> conn) const;

    /**
     * Retrieves a database connection from the connection pool.
     * Waits for a connection to become available if the pool is empty.
     *
     * @return A unique pointer to the pqxx connection.
     */
    std::unique_ptr<pqxx::connection> GetConnection() const;

    /**
     * Updates the checkpoint for a specific chunk.
     *
     * @param chunkStartHeight The starting height of the chunk.
     * @param checkpointUpdateValue The value to update the checkpoint to.
     */
    void UpdateChunkCheckpoint(size_t chunkStartHeight, size_t checkpointUpdateValue) const;

    /**
     * Adds a block height to a list or set of missed blocks.
     *
     * @param blockHeight The height of the block that was missed.
     */
    void AddMissedBlock(size_t blockHeight) const;

    /**
     * Removes a block height from the list or set of missed blocks.
     *
     * @param blockHeight The height of the block to remove from missed blocks.
     */
    void RemoveMissedBlock(size_t blockHeight) const;

    /**
     * Establishes connections to the database.
     *
     * @param poolSize The number of connections to establish in the connection pool.
     * @param connection_string The connection string used to connect to the database.
     */
    void Connect(size_t poolSize, const std::string& connection_string);

    /**
     * Creates necessary tables in the database.
     *
     * @return True if tables are successfully created, false otherwise.
     */
    void CreateTables();

    /**
     * Creates a checkpoint if it does not exist.
     *
     * @param chunkStartHeight The starting height of the chunk.
     * @param chunkEndHeight The ending height of the chunk.
     */
    void CreateCheckpointIfNonExistent(size_t chunkStartHeight, size_t chunkEndHeight) const;

    /**
     * Loads and processes chunks that have not been processed yet.
     */
    void LoadAndProcessUnprocessedChunks();

    /**
     * Stores a chunk of data in the database.
     *
     * @param isTrackingCheckpointForChunk Indicates if a checkpoint is being tracked for this chunk.
     * @param chunk The chunk of data to store.
     * @param chunkStartHeight The starting height of the chunk.
     * @param chunkEndHeight The ending height of the chunk.
     * @param trueRangeStartHeight The true starting height of the range. Defaults to 0 if not provided.
     */
    void StoreChunk(bool isTrackingCheckpointForChunk, const std::vector<Json::Value> &chunk, uint64_t  chunkStartHeight, uint64_t  chunkEndHeight, uint64_t trueRangeStartHeight = Database::InvalidHeight) const;

    /**
     * Stores transactions related to a block in the database.
     *
     * @param block The block containing the transactions.
     * @param conn A unique pointer to the database connection.
     * @param blockTransaction The transaction object for the block.
     */
    void StoreTransactions(const Json::Value& block, const std::unique_ptr<pqxx::connection>& conn, pqxx::work &blockTransaction) const;

    /**
     * Stores connected peers to the peersinfo table.
     * 
     * @param peer_info JSON array containing information about connected peers.
    */
    void StorePeers(const Json::Value& peer_info) const;

    void StoreChainInfo(const Json::Value& chain_info) const;
    
public:
 static const uint64_t InvalidHeight{std::numeric_limits<uint64_t>::max()};
 struct Checkpoint {
        size_t chunkStartHeight;
        size_t chunkEndHeight;
        size_t lastCheckpoint;
    };

    /**
     * Constructs the Database object.
     */
    Database() = default;

    /**
     * Destructs the Database object and releases resources.
     */
    ~Database();

    Database(const Database& rhs) noexcept = delete;
    Database& operator=(const Database& rhs) noexcept = delete;

    Database(Database&& rhs) noexcept = default;
    Database& operator=(Database&& rhs) noexcept = default;

    uint64_t GetSyncedBlockCountFromDB() const;
    std::optional<pqxx::row> GetOutputByTransactionIdAndIndex(const std::string& txid, uint64_t v_out_index) const;
    std::stack<Database::Checkpoint> GetUnfinishedCheckpoints() const;
    std::optional<Database::Checkpoint> GetCheckpoint(signed int chunkStartHeight) const;
};

#endif // DATABASE_H