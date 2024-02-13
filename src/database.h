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
#include <string>
#include <iostream>
#include <variant>
#include <limits>
#include <jsonrpccpp/common/jsonparser.h>

#include "httpclient.h"
#include "logger.h"
#include "controller.h"
#include "chain_resource.h"

#ifndef DATABASE_H
#define DATABASE_H

class ManagedConnection;

class Database
{

    friend class Controller;
    friend class Syncer;

private:
    static std::queue<std::unique_ptr<pqxx::connection>> connection_pool;
    static std::mutex cs_connection_pool;
    static std::condition_variable cv_connection_pool;

    static bool is_connected;
    static bool is_database_setup;

    void BatchInsertStatements(pqxx::work &batch_insert_txn, const std::string& table_name, const std::vector<std::string> &columns, const std::vector<std::vector<BlockData>>&) const;

    /**
     * Shuts down all connections in the connection pool.
     * Closes each connection and clears the pool.
     */
    void ShutdownConnections();

    /**
     * Updates the checkpoint for a specific chunk.
     *
     * @param chunkStartHeight The starting height of the chunk.
     * @param checkpointUpdateValue The value to update the checkpoint to.
     */
    void UpdateChunkCheckpoint(size_t chunkStartHeight, size_t checkpointUpdateValue);

    /**
     * Adds a block height to a list or set of missed blocks.
     *
     * @param blockHeight The height of the block that was missed.
     */
    void AddMissedBlock(size_t blockHeight);

    /**
     * Removes a block height from the list or set of missed blocks.
     *
     * @param blockHeight The height of the block to remove from missed blocks.
     */
    void RemoveMissedBlock(size_t blockHeight);

    /**
     * Establishes connections to the database.
     *
     * @param poolSize The number of connections to establish in the connection pool.
     * @param connection_string The connection string used to connect to the database.
     */
    void Connect(size_t poolSize, const std::string &connection_string);

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
    void CreateCheckpointIfNonExistent(size_t chunkStartHeight, size_t chunkEndHeight);

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
    void BatchStoreBlocks(std::vector<Block> &chunk, uint64_t chunkStartHeight, uint64_t chunkEndHeight, uint64_t trueRangeStartHeight = Database::InvalidHeight);
    
    /**
     * Stores connected peers to the peersinfo table.
     *
     * @param peer_info JSON array containing information about connected peers.
    */
    void StorePeers(const Json::Value& peer_info);

    void StoreChainInfo(const Json::Value& chain_info);
    
public:
    static const uint64_t InvalidHeight{std::numeric_limits<uint64_t>::max()};

    struct Checkpoint
    {
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

    Database(const Database &rhs) noexcept = delete;
    Database &operator=(const Database &rhs) noexcept = delete;

    Database(Database &&rhs) noexcept = default;
    Database &operator=(Database &&rhs) noexcept = default;

    /*
     * Releases a database connection back to the connection pool.
     *
     * @param conn A unique pointer to the pqxx connection to be released.
     */
    void ReleaseConnection(std::unique_ptr<pqxx::connection> conn);

    /**
     * Retrieves a database connection from the connection pool.
     * Waits for a connection to become available if the pool is empty.
     *
     * @return A unique pointer to the pqxx connection.
     */
    std::unique_ptr<pqxx::connection> GetConnection();

  template <typename... Args>
static std::optional<const pqxx::result> ExecuteRead(std::string sql, Args... args)
{
    try
    {
        std::unique_lock<std::mutex> lock(Database::cs_connection_pool);
        auto conn = std::move(Database::connection_pool.front());
        connection_pool.pop();

        if (conn == nullptr || !conn->is_open())
        {
            __ERROR__("------Invalid connection: connection is null or not open.--------");
            throw std::runtime_error("------Invalid connection: connection is null or not open.--------");
        }

        pqxx::read_transaction read_transaction(*conn);
        pqxx::result read_result = read_transaction.exec_params(sql, args...);
        read_transaction.commit();
        return read_result;
    }
    catch (const std::exception &e)
    {
        __ERROR__(e.what());
        return std::nullopt;
    }
}

    // Functions related to the indexing process
    uint64_t GetSyncedBlockCountFromDB();
    std::optional<pqxx::row> GetOutputByTransactionIdAndIndex(const std::string &txid, uint64_t v_out_index);
    std::stack<Database::Checkpoint> GetUnfinishedCheckpoints();
    std::optional<Database::Checkpoint> GetCheckpoint(signed int chunkStartHeight);
};

class ManagedConnection {
public:
    ManagedConnection(Database& db) : db_(db), conn_(db.GetConnection()) {}

    ~ManagedConnection() {
        db_.ReleaseConnection(std::move(conn_));
    }

// Overload the dereference operator to return a reference to the pqxx::connection
    pqxx::connection& operator*() const {
        return *conn_;
    }

    // Overload the arrow operator to return the pointer to pqxx::connection
    pqxx::connection* operator->() const {
        return conn_.get();
    }

private:
    Database& db_;
    std::unique_ptr<pqxx::connection> conn_;
};

#endif // DATABASE_H
