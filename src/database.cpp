#include "database.h"
#include <fstream>
#include <string>
#include <iostream>
#include <memory>

template std::optional<const pqxx::result> Database::ExecuteRead<std::string, uint64_t>(const char *sql, std::string, uint64_t);

const uint64_t Database::InvalidHeight;
bool Database::is_connected = false;
bool Database::is_database_setup = false;

std::queue<std::unique_ptr<pqxx::connection>> Database::connection_pool;
std::mutex Database::cs_connection_pool;
std::condition_variable Database::cv_connection_pool;

Database::~Database()
{
    ShutdownConnections();
}

void Database::Connect(size_t poolSize, const std::string &conn_str)
{
    __INFO__("Attempting to connect to database and initialize connection pool.");

    if (is_connected)
    {
        __INFO__("Database is already connected.");
        return;
    }

    try
    {
        for (size_t i = 0; i < poolSize; ++i)
        {
            auto conn = std::make_unique<pqxx::connection>(conn_str);
            connection_pool.push(std::move(conn));
        }

        is_connected = true;
    }
    catch (std::exception &e)
    {
        is_connected = false;
        ShutdownConnections();
        // TODO: ClearPool();

        std::stringstream err_stream;
        err_stream << "Error occurred while creating connection pool: " << e.what() << std::endl;
        __ERROR__(err_stream.str().c_str());
        throw std::runtime_error(err_stream.str());
    }
}

std::unique_ptr<pqxx::connection> Database::GetConnection()
{
    __INFO__("GetConnection()");
    try
    {
        std::unique_lock<std::mutex> lock(cs_connection_pool);
        cv_connection_pool.wait(lock, []
                                { return !connection_pool.empty(); });

        auto conn = std::move(connection_pool.front());
        connection_pool.pop();

        if (conn == nullptr || !conn->is_open())
        {
            __ERROR__("------Invalid connection: connection is null or not open.--------");
            throw std::runtime_error("------Invalid connection: connection is null or not open.--------");
        }

        return conn;
    }
    catch (std::exception &e)
    {
        __ERROR__(e.what());

        std::stringstream err_stream;
        err_stream << e.what() << std::endl;

        throw std::runtime_error(e.what());
    }
}

void Database::ReleaseConnection(std::unique_ptr<pqxx::connection> conn)
{
    __INFO__("ReleaseConnection()");

    try
    {
        if (conn == nullptr || !conn->is_open())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(cs_connection_pool);
        connection_pool.push(std::move(conn));
        cv_connection_pool.notify_one();
    }
    catch (const std::exception &e)
    {
        __ERROR__(e.what());

        throw std::runtime_error(e.what());
    }
}

void Database::ShutdownConnections()
{
    std::lock_guard<std::mutex> lock(cs_connection_pool);
    while (!connection_pool.empty())
    {
        auto &conn = connection_pool.front();
        if (conn->is_open())
        {
            conn->close();
        }
        connection_pool.pop();
    }
}

void Database::CreateTables()
{
    if (is_database_setup)
    {
        std::cerr << "Database has already been setup in the current instance. Exiting function Database::CreateTables()" << std::endl;
        return;
    }

    std::unique_ptr<pqxx::connection> conn = Database::GetConnection();
    std::string_view createTableStatements[7]{"CREATE TABLE blocks ("
                                              "hash TEXT PRIMARY KEY, "
                                              "height INTEGER, "
                                              "timestamp INTEGER, "
                                              "nonce TEXT,"
                                              "size INTEGER,"
                                              "num_transactions INTEGER,"
                                              "total_block_output DOUBLE PRECISION, "
                                              "difficulty DOUBLE PRECISION, "
                                              "chainwork TEXT, "
                                              "merkle_root TEXT, "
                                              "version INTEGER, "
                                              "bits TEXT, "
                                              "transaction_ids TEXT[], "
                                              "num_outputs INTEGER, "
                                              "num_inputs INTEGER, "
                                              "total_block_input DOUBLE PRECISION, "
                                              "miner TEXT"
                                              ")",
                                              "CREATE TABLE transactions ("
                                              "tx_id TEXT PRIMARY KEY, "
                                              "size INTEGER, "
                                              "is_overwintered TEXT, "
                                              "version INTEGER, "
                                              "total_public_input TEXT, "
                                              "total_public_output TEXT, "
                                              "hex TEXT, "
                                              "hash TEXT, "
                                              "timestamp INTEGER, "
                                              "height INTEGER, "
                                              "num_inputs INTEGER, "
                                              "num_outputs INTEGER"
                                              ")",
                                              "CREATE TABLE checkpoints ("
                                              "chunk_start_height INTEGER PRIMARY KEY,"
                                              "chunk_end_height INTEGER,"
                                              "last_checkpoint INTEGER"
                                              ")",
                                              "CREATE TABLE transparent_inputs ("
                                              "tx_id TEXT PRIMARY KEY, "
                                              "vin_tx_id TEXT, "
                                              "v_out_idx INTEGER, "
                                              "value DOUBLE PRECISION, "
                                              "senders TEXT[], "
                                              "coinbase TEXT)",
                                              "CREATE TABLE transparent_outputs ("
                                              "tx_id TEXT, "
                                              "output_index INTEGER, "
                                              "recipients TEXT[], "
                                              "value TEXT)",
                                              "CREATE TABLE peerinfo (addr TEXT, lastsend TEXT, lastrecv TEXT, conntime TEXT, subver TEXT, synced_blocks TEXT)",
                                              "CREATE TABLE chain_info (orchard_pool_value DOUBLE PRECISION, best_block_hash TEXT, size_on_disk DOUBLE PRECISION, best_height INT, total_chain_value DOUBLE PRECISION)"};

    try
    {
        pqxx::work tx(*conn);

        for (const std::string_view &query : createTableStatements)
        {
            try
            {
                tx.exec(query.data());
            }
            catch (const pqxx::sql_error &e)
            {
                // SQL Error Codes: https://www.postgresql.org/docs/15/errcodes-appendix.html

                if (e.sqlstate() == "42P07")
                {
                    __DEBUG__(e.what()); // DUPLICATE_TABLE::Table already exists
                }
                else
                {
                    __DEBUG__(e.sqlstate().c_str());
                    __DEBUG__(e.what());
                    __DEBUG__("Aborting create table operations.");

                    tx.abort();
                    is_database_setup = false;
                    throw;
                }
            }
        }

        tx.commit();
        is_database_setup = true;
        Database::ReleaseConnection(std::move(conn));
    }
    catch (const pqxx::sql_error &e)
    {
        Database::ReleaseConnection(std::move(conn));
        throw;
    }
    catch (const std::exception &e)
    {
        Database::ReleaseConnection(std::move(conn));
        throw;
    }
}

void Database::UpdateChunkCheckpoint(size_t chunkStartHeight, size_t currentProcessingChunkHeight)
{

    std::unique_ptr<pqxx::connection> conn = Database::GetConnection();

    try
    {

        pqxx::work transaction(*conn.get());

        conn->prepare(
            "update_checkpoint",
            "UPDATE checkpoints "
            "SET last_checkpoint = $2 "
            "WHERE chunk_start_height = $1;");

        transaction.exec_prepared("update_checkpoint", chunkStartHeight, currentProcessingChunkHeight);
        transaction.commit();

        std::string message = "Updating checkpoint from start value " + std::to_string(chunkStartHeight) + " to " + std::to_string(currentProcessingChunkHeight);
        __DEBUG__(message.c_str());

        conn->unprepare("update_checkpoint");
        Database::ReleaseConnection(std::move(conn));
    }
    catch (std::exception &e)
    {
        __ERROR__(e.what());
        Database::ReleaseConnection(std::move(conn));

        throw;
    }
}

std::optional<Database::Checkpoint> Database::GetCheckpoint(signed int chunkStartHeight)
{
    if (chunkStartHeight == Database::InvalidHeight)
    {
        return std::nullopt;
    }

    std::unique_ptr<pqxx::connection> conn = Database::GetConnection();

    try
    {

        pqxx::work transaction(*conn.get());

        pqxx::result result = transaction.exec(
            "SELECT chunk_start_height, chunk_end_height, last_checkpoint "
            "FROM checkpoints WHERE chunk_start_height = " +
            transaction.quote(chunkStartHeight));

        if (result.empty())
        {
            Database::ReleaseConnection(std::move(conn));
            return std::nullopt;
        }
        else
        {
            pqxx::row row = result[0];

            Checkpoint checkpoint;

            checkpoint.chunkStartHeight = row["chunk_start_height"].as<size_t>();
            checkpoint.chunkEndHeight = row["chunk_end_height"].as<size_t>();
            checkpoint.lastCheckpoint = row["last_checkpoint"].as<size_t>();

            Database::ReleaseConnection(std::move(conn));
            return checkpoint;
        }
    }
    catch (const pqxx::sql_error &e)
    {
        __ERROR__(e.what());
        Database::ReleaseConnection(std::move(conn));
        throw;
    }
    catch (const std::exception &e)
    {
        __ERROR__(e.what());
        Database::ReleaseConnection(std::move(conn));
        throw;
    }
}

void Database::CreateCheckpointIfNonExistent(size_t chunkStartHeight, size_t chunkEndHeight)
{
    try
    {

        std::string insertCheckpointStatement = R"(
    INSERT INTO checkpoints (
        chunk_start_height, 
        chunk_end_height, 
        last_checkpoint
    ) VALUES ($1, $2, $3);
)";

        std::unique_ptr<pqxx::connection> conn = Database::GetConnection();
        conn->prepare("insert_checkpoint", insertCheckpointStatement);
        pqxx::work transaction(*conn.get());

        transaction.exec_prepared("insert_checkpoint", chunkStartHeight, chunkEndHeight, chunkStartHeight);
        transaction.commit();

        __DEBUG__(("Checkpoint created at height " + std::to_string(chunkStartHeight) + " to " + std::to_string(chunkEndHeight)).c_str());

        conn->unprepare("insert_checkpoint");

        Database::ReleaseConnection(std::move(conn));
    }
    catch (std::exception &e)
    {
        __ERROR__(e.what());
    }
}

std::stack<Database::Checkpoint> Database::GetUnfinishedCheckpoints()
{
    std::unique_ptr<pqxx::connection> conn = Database::GetConnection();

    try
    {
        // Obtain checkpoints
        pqxx::work transaction(*conn.get());

        std::string query = R"(
            SELECT chunk_start_height, chunk_end_height, last_checkpoint
            FROM checkpoints
            WHERE chunk_end_height != last_checkpoint
        )";

        // Execute query
        pqxx::result result = transaction.exec(query);

        // Process the sql rows for each checkpoint
        std::stack<Checkpoint> checkpoints;
        Checkpoint currentCheckpoint;

        pqxx::result::const_iterator row_iterator = result.cbegin();
        while (row_iterator != result.cend())
        {
            currentCheckpoint.chunkStartHeight = row_iterator["chunk_start_height"].as<size_t>();
            currentCheckpoint.chunkEndHeight = row_iterator["chunk_end_height"].as<size_t>();
            currentCheckpoint.lastCheckpoint = row_iterator["last_checkpoint"].as<size_t>();
            checkpoints.push(currentCheckpoint);

            ++row_iterator;
        }

        Database::ReleaseConnection(std::move(conn));
        return checkpoints;
    }
    catch (const pqxx::sql_error &e)
    {
        __ERROR__(e.what());
        Database::ReleaseConnection(std::move(conn));

        std::stack<Database::Checkpoint> empty_stack;
        return empty_stack;
    }
    catch (const std::exception &e)
    {
        __ERROR__(e.what());
        Database::ReleaseConnection(std::move(conn));

        std::stack<Database::Checkpoint> empty_stack;
        return empty_stack;
    }
}

void Database::AddMissedBlock(size_t blockHeight) const
{
    __DEBUG__(("Missed block at height " + std::to_string(blockHeight)).c_str());
}

void Database::RemoveMissedBlock(size_t blockHeight) const
{
    __DEBUG__(("Recovered block at height " + std::to_string(blockHeight)).c_str());
}

template <typename... Args>
std::optional<const pqxx::result> Database::ExecuteRead(const char *sql, Args... args)
{
    try
    {
        auto conn = Database::GetConnection();
        pqxx::work txn(*conn);

        auto result = txn.exec_params(sql, args...);
        txn.commit();

        return result;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Database read failed: " << e.what() << std::endl;
    }
}

void Database::StoreChunk(const std::vector<Block> &chunk, uint64_t chunkStartHeight, uint64_t chunkEndHeight, uint64_t trueRangeStartHeight)
{
    __INFO__("Syncing path: StoreChunk()");
    std::unique_ptr<pqxx::connection> conn = Database::GetConnection();
    pqxx::work blockTransaction(*conn.get());

    std::optional<Database::Checkpoint> checkpointOpt = this->GetCheckpoint(trueRangeStartHeight);
    bool checkpointExist = checkpointOpt.has_value();

    bool shouldCommitBlock{true};
    auto timeSinceLastCheckpoint = std::chrono::steady_clock::now();
    size_t chunkCurrentProcessingIndex{static_cast<size_t>(chunkStartHeight)};

    for (const auto &item : chunk)
    {
        if (!item.isValid())
        {
            ++chunkCurrentProcessingIndex;
            continue;
        }
        try 
        {   
            // @dev Eventually this method should receive the StoreableBlockData from GetStoreableBlock()
            // and handle the execution of prepared statements here.
            item.GetStoreableBlockData().ProcessBlockToStoreable(blockTransaction, conn);
        }
        catch (const pqxx::sql_error &e)
        {
            __ERROR__(e.what());
            shouldCommitBlock = false;
        }
        catch (const std::exception &e)
        {
            __ERROR__(e.what());
            shouldCommitBlock = false;
        }

        if (shouldCommitBlock)
        {
            blockTransaction.commit();
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsedTimeSinceLastCheckpoint = now - timeSinceLastCheckpoint;

        Database::Checkpoint checkpoint;
        if (checkpointExist)
        {
            checkpoint = checkpointOpt.value();

            bool reachedEndOfChunk = chunkCurrentProcessingIndex == chunkEndHeight;
            // Record a checkpoint if 10 seconds has elapsed since the last or the end of the chunk has been reached
            if (elapsedTimeSinceLastCheckpoint >= std::chrono::seconds(10) || reachedEndOfChunk)
            {

                this->UpdateChunkCheckpoint(checkpointExist ? checkpoint.chunkStartHeight : chunkStartHeight, chunkCurrentProcessingIndex);
                timeSinceLastCheckpoint = now;

                if (reachedEndOfChunk)
                {
                    break;
                }
            }
        }

        ++chunkCurrentProcessingIndex;
        shouldCommitBlock = true;
    }

    conn->unprepare("insert_block");
    conn->unprepare("insert_transactions");
    conn->unprepare("insert_transparent_inputs");
    conn->unprepare("insert_transparent_outputs");
    Database::ReleaseConnection(std::move(conn));
}

std::optional<pqxx::row> Database::GetOutputByTransactionIdAndIndex(const std::string &txid, uint64_t v_out_index)
{
    std::unique_ptr<pqxx::connection> conn = Database::GetConnection();
    pqxx::work tx(*conn.get());

    pqxx::result result = tx.exec_params("SELECT * FROM transparent_outputs WHERE tx_id = $1 AND output_index = $2", txid, v_out_index);

    Database::ReleaseConnection(std::move(conn));

    if (!result.empty())
    {
        return result[0];
    }

    return std::nullopt;
}

uint64_t Database::GetSyncedBlockCountFromDB()
{
    std::unique_ptr<pqxx::connection> conn = Database::GetConnection();

    try
    {
        pqxx::work tx(*conn.get());
        std::optional<pqxx::row> row = tx.exec1("SELECT height FROM blocks ORDER BY height DESC LIMIT 1;");
        size_t syncedBlockCount{0};
        if (row.has_value())
        {
            syncedBlockCount = row.value()[0].as<int>();
        }
        else
        {
            syncedBlockCount = 0;
        }

        __DEBUG__(("New synced block count " + std::to_string(syncedBlockCount)).c_str());

        Database::ReleaseConnection(std::move(conn));
        return syncedBlockCount;
    }
    catch (std::exception &e)
    {
        __ERROR__(e.what());
        Database::ReleaseConnection(std::move(conn));
        return 0;
    }
    catch (pqxx::unexpected_rows &e)
    {
        __ERROR__(e.what());
        Database::ReleaseConnection(std::move(conn));
        return 0;
    }
}

void Database::StorePeers(const Json::Value &peer_info)
{
    std::unique_ptr<pqxx::connection> connection = Database::GetConnection();
    try
    {

        if (peer_info.isNull())
        {
            Database::ReleaseConnection(std::move(connection));
            return;
        }

        pqxx::work tx{*connection.get()};

        // Clear existing peers
        tx.exec("TRUNCATE TABLE peerinfo;");
        tx.commit();

        connection->prepare("insert_peer_info", "INSERT INTO peerinfo (addr, lastsend, lastrecv, conntime, subver, synced_blocks) VALUES ($1, $2, $3, $4, $5, $6)");

        if (peer_info.isArray() && peer_info.size() > 0)
        {
            for (const Json::Value &peer : peer_info)
            {
                tx.exec_prepared("insert_peer_info", peer["addr"].asString(), peer["lastsend"].asString(), peer["lastrecv"].asString(), peer["conntime"].asString(), peer["subver"].asString(), peer["synced_blocks"].asString());
            }

            tx.commit();
        }

        Database::ReleaseConnection(std::move(connection));
    }
    catch (const std::exception &e)
    {
        __ERROR__(e.what());
        Database::ReleaseConnection(std::move(connection));
    }
}

void Database::StoreChainInfo(const Json::Value &chain_info)
{

    if (!chain_info.isNull())
    {
        const char *insert_chain_info_query{"INSERT INTO chain_info (orchard_pool_value, best_block_hash, size_on_disk, best_height, total_chain_value) VALUES ($1, $2, $3, $4, $5)"};
        auto conn = Database::GetConnection();

        double orchardPoolValue{0.0};
        Json::Value valuePools = chain_info["valuePools"];
        double totalChainValueAccumulator{0.0};
        if (!valuePools.isNull())
        {
            for (const Json::Value &pool : valuePools)
            {
                if (pool["id"].asString() == "orchard")
                {
                    orchardPoolValue = pool["chainValue"].asDouble();
                }

                totalChainValueAccumulator += pool["chainValue"].asDouble();
            }
        }

        try
        {
            pqxx::work tx{*conn.get()};
            tx.exec_params(insert_chain_info_query, orchardPoolValue, chain_info["bestblockhash"].asString(), chain_info["size_on_disk"].asDouble(), chain_info["estimatedheight"].asInt(), totalChainValueAccumulator);
            tx.commit();
            Database::ReleaseConnection(std::move(conn));
            delete insert_chain_info_query;
        }
        catch (const std::exception &e)
        {
            __ERROR__(e.what());
            delete insert_chain_info_query;
            Database::ReleaseConnection(std::move(conn));
        }
    }
}