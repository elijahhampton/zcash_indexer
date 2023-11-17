#include <memory>
#include <pqxx/pqxx>
#include <queue>
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

#include <jsonrpccpp/common/jsonparser.h>

class Database {

private:
    std::queue<std::unique_ptr<pqxx::connection>> connectionPool;
    std::mutex poolMutex;
    std::condition_variable poolCondition;

/**
     * Shuts down all connections in the connection pool.
     * Closes each connection and clears the pool.
     */
    void ShutdownConnections();

    /**
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
     * Checks if DB contains all entries from block 0 to chain height based height
     * 
     * @note This does not mean the DB is correct. It only gives a theoretical view of all blocks that have been synced since 0.
     * @return True if the entire range is complete, false otherwise.
     */
    bool isWhole();

public:
    /**
     * Constructs the Database object.
     */
    Database();

    /**
     * Destructs the Database object and releases resources.
     */
    ~Database();

    /**
     * Establishes connections to the database.
     *
     * @param poolSize The number of connections to establish in the connection pool.
     * @param connection_string The connection string used to connect to the database.
     * @return True if connections are successfully established, false otherwise.
     */
    bool Connect(size_t poolSize, const std::string& connection_string);

    /**
     * Creates necessary tables in the database.
     *
     * @return True if tables are successfully created, false otherwise.
     */
    bool CreateTables();

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
     * @param lastCheckpoint The last processed checkpoint. Defaults to -1 if not provided.
     * @param trueRangeStartHeight The true starting height of the range. Defaults to -1 if not provided.
     */
    void StoreChunk(bool isTrackingCheckpointForChunk, const std::vector<Json::Value> &chunk, signed int chunkStartHeight, signed int chunkEndHeight, signed int lastCheckpoint = -1, signed int trueRangeStartHeight = -1);

    /**
     * Stores transactions related to a block in the database.
     *
     * @param block The block containing the transactions.
     * @param conn A unique pointer to the database connection.
     * @param blockTransaction The transaction object for the block.
     */
    void StoreTransactions(const Json::Value& block, const std::unique_ptr<pqxx::connection>& conn, pqxx::work &blockTransaction);

};
