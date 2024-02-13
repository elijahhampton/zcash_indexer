#include "database.h"
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

template std::optional<const pqxx::result> Database::ExecuteRead<std::string, uint64_t>(std::string sql, std::string, uint64_t);

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

    __DEBUG__(("Initializing database pool with " + std::to_string(poolSize) + " connections").c_str());
    try
    {
        for (size_t i = 0; i < poolSize; ++i)
        {
            auto conn = std::make_unique<pqxx::connection>(conn_str);
            conn->set_verbosity(pqxx::error_verbosity::verbose);

            connection_pool.push(std::move(conn));
            __DEBUG__(("Completed connections " + std::to_string((i + 1)) + "/" + std::to_string(poolSize)).c_str());
        }

        is_connected = true;
    }
    catch (std::exception &e)
    {
        is_connected = false;
        ShutdownConnections();
        // TODO: ClearPool();

        __ERROR__(e.what());
        throw std::runtime_error(e.what());
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
        std::cout << "DSFSDF" << std::endl;
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

    ManagedConnection conn(*this);

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

                if (e.sqlstate() == "42P07" || e.sqlstate() == "25P02")
                {
                    __ERROR__(e.what()); // DUPLICATE_TABLE::Table already exists
                }
                else
                {
                    __ERROR__(e.sqlstate().c_str());
                    __ERROR__(e.what());
                    __INFO__("Aborting create table operations.");

                    tx.abort();
                    is_database_setup = false;
                    throw;
                }
            }
        }

        tx.commit();
        this->is_database_setup = true;
    }
    catch (const pqxx::sql_error &e)
    {
        throw;
    }
    catch (const std::exception &e)
    {
        throw;
    }
}

void Database::BatchInsertStatements(pqxx::work &batch_insert_txn, const std::string &table_name, const std::vector<std::string> &columns, const std::vector<std::vector<BlockData>> &orm_values) const
{
    try
    {
        std::stringstream query;
        query << "INSERT INTO ";

        query << table_name << " (";
        for (size_t j = 0; j < columns.size(); ++j)
        {
            query << columns[j];

            if (j != columns.size() - 1)
            {
                query << ", ";
            }
            else
            {
                query << ") VALUES ";
            }
        }

        for (size_t i = 0; i < orm_values.size(); ++i)
        {
            const auto &row = orm_values[i];
            query << "(";

            for (size_t j = 0; j < row.size(); ++j)
            {
                // std::visit is used to obtain the string representation of the variant
                std::string value = std::visit([](auto &&arg) -> std::string
                                               {
                                                   using T = std::decay_t<decltype(arg)>;
                                                   if constexpr (std::is_same_v<T, std::string>)
                                                       return arg; // If it's a string, use it directly
                                                   else
                                                       return std::to_string(arg); // Convert numbers to string
                                                   // Handle other types as needed
                                               },
                                               row[j]);

                query << batch_insert_txn.quote(value); // Use the obtained string value
                if (j < row.size() - 1)
                {
                    query << ", ";
                }
            }
            query << ")";

            if (i < orm_values.size() - 1)
            {
                query << ", ";
            }
        }

        query << ";";
        batch_insert_txn.exec(query.str());
    }
    catch (const std::exception &e)
    {
        throw;
    }
}

void Database::UpdateChunkCheckpoint(size_t chunkStartHeight, size_t currentProcessingChunkHeight)
{

    ManagedConnection conn(*this);

    try
    {
        pqxx::work transaction(*conn);

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
    }
    catch (std::exception &e)
    {
        __ERROR__(e.what());
        throw;
    }
}

std::optional<Database::Checkpoint> Database::GetCheckpoint(signed int chunkStartHeight)
{
    if (chunkStartHeight == Database::InvalidHeight)
    {
        return std::nullopt;
    }
    ManagedConnection conn(*this);
    try
    {

        pqxx::work transaction(*conn);

        pqxx::result result = transaction.exec(
            "SELECT chunk_start_height, chunk_end_height, last_checkpoint "
            "FROM checkpoints WHERE chunk_start_height = " +
            transaction.quote(chunkStartHeight));

        if (result.empty())
        {
            return std::nullopt;
        }
        else
        {
            pqxx::row row = result[0];

            Checkpoint checkpoint;

            checkpoint.chunkStartHeight = row["chunk_start_height"].as<size_t>();
            checkpoint.chunkEndHeight = row["chunk_end_height"].as<size_t>();
            checkpoint.lastCheckpoint = row["last_checkpoint"].as<size_t>();

            return checkpoint;
        }
    }
    catch (const pqxx::sql_error &e)
    {
        __ERROR__(e.what());
        throw;
    }
    catch (const std::exception &e)
    {
        __ERROR__(e.what());
        throw;
    }
}

void Database::CreateCheckpointIfNonExistent(size_t chunkStartHeight, size_t chunkEndHeight)
{
    ManagedConnection conn(*this);

    try
    {

        std::string insertCheckpointStatement = R"(
    INSERT INTO checkpoints (
        chunk_start_height, 
        chunk_end_height, 
        last_checkpoint
    ) VALUES ($1, $2, $3);
)";

        conn->prepare("insert_checkpoint", insertCheckpointStatement);
        pqxx::work transaction(*conn);

        transaction.exec_prepared("insert_checkpoint", chunkStartHeight, chunkEndHeight, chunkStartHeight);
        transaction.commit();

        __DEBUG__(("Checkpoint created at height " + std::to_string(chunkStartHeight) + " to " + std::to_string(chunkEndHeight)).c_str());

        conn->unprepare("insert_checkpoint");
    }
    catch (std::exception &e)
    {
        __ERROR__(e.what());
    }
}

std::stack<Database::Checkpoint> Database::GetUnfinishedCheckpoints()
{
    ManagedConnection conn(*this);

    try
    {

        pqxx::work transaction(*conn);

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
        return checkpoints;
    }
    catch (const pqxx::sql_error &e)
    {
        __ERROR__(e.what());

        std::stack<Database::Checkpoint> empty_stack;
        return empty_stack;
    }
    catch (const std::exception &e)
    {
        __ERROR__(e.what());

        std::stack<Database::Checkpoint> empty_stack;
        return empty_stack;
    }
}

void Database::AddMissedBlock(size_t blockHeight)
{
    __DEBUG__(("Missed block at height " + std::to_string(blockHeight)).c_str());
}

void Database::BatchStoreBlocks(std::vector<Block> &chunk, uint64_t chunkStartHeight, uint64_t chunkEndHeight, uint64_t trueRangeStartHeight)
{
    __INFO__("Syncing path: BatchStoreBlocks()");

    std::optional<Database::Checkpoint> checkpointOpt = this->GetCheckpoint(trueRangeStartHeight);
    bool checkpointExist = checkpointOpt.has_value();

    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastCheckpoint = now;
    auto elapsedTimeSinceLastCheckpoint{now - timeSinceLastCheckpoint};
    size_t chunkCurrentProcessingIndex{static_cast<size_t>(chunkStartHeight)};

    ManagedConnection conn(*this);
    pqxx::work batch_insert_txn(*conn);

    // Process the chunk. Commit all transactions by the block (i.e. batch insert transaction is atomic)
    for (auto &item : chunk)
    {
        if (!item.isValid())
        {
            throw std::invalid_argument("Expected Json::Value for block, but found Json::nullValue");
        }

        // Obtain orm storage map and batch inserts for the entire block
        try
        {
            std::map<std::string, std::vector<std::vector<BlockData>>> orm_storage_map = item.DataToOrmStorageMap();

            for (auto iter = orm_storage_map.cbegin(); iter != orm_storage_map.cend(); ++iter)
            {
                for (auto stmt_iter = orm_storage_map[iter->first].cbegin(); stmt_iter != orm_storage_map[iter->first].cend(); ++stmt_iter)
                {
                    const auto &tableName = iter->first;
                    const auto &tableData = iter->second;

                    if (tableName == "blocks")
                    {
                        this->BatchInsertStatements(batch_insert_txn, tableName,
                                                    {"hash", "height", "timestamp", "nonce", "size", "num_transactions", "total_block_output",
                                                     "difficulty", "chainwork", "merkle_root", "version", "bits", "transaction_ids", "num_outputs",
                                                     "num_inputs", "total_block_input", "miner"},
                                                    tableData);
                    }
                    else if (tableName == "transactions")
                    {
                        this->BatchInsertStatements(batch_insert_txn, tableName, {"tx_id", "size", "is_overwintered", "version", "total_public_input", "total_public_output", "hex", "hash", "timestamp", "height", "num_inputs", "num_outputs"}, tableData);
                    }
                    else if (tableName == "transparent_inputs")
                    {
                        this->BatchInsertStatements(batch_insert_txn, tableName, {"tx_id", "vin_tx_id", "v_out_idx", "value", "senders", "coinbase"}, tableData);
                    }
                    else if (tableName == "transparent_outputs")
                    {
                        this->BatchInsertStatements(batch_insert_txn, tableName, {"tx_id", "output_index", "recipients", "value"}, tableData);
                    }
                }
            }

            batch_insert_txn.commit();
        }
        catch (const std::exception &e)
        {
            // Missed block
            __ERROR__(e.what());

            ++chunkCurrentProcessingIndex;
            continue;
        }

        // Check elapsed time since last chckpoint and attempt an update
        now = std::chrono::steady_clock::now();
        elapsedTimeSinceLastCheckpoint = now - timeSinceLastCheckpoint;

        Database::Checkpoint checkpoint;
        if (checkpointExist)
        {
            checkpoint = checkpointOpt.value();
            bool reachedEndOfChunk = chunkCurrentProcessingIndex == chunkEndHeight;

            // Update checkpoint if 10 seconds has elapsed since the last or the end of the chunk has been reached
            if (elapsedTimeSinceLastCheckpoint >= std::chrono::seconds(10) || reachedEndOfChunk)
            {
                this->UpdateChunkCheckpoint(checkpointExist ? checkpoint.chunkStartHeight : chunkStartHeight, chunkCurrentProcessingIndex);
                timeSinceLastCheckpoint = std::chrono::steady_clock::now();
            }
        }

        ++chunkCurrentProcessingIndex;
    }
}

std::optional<pqxx::row> Database::GetOutputByTransactionIdAndIndex(const std::string &txid, uint64_t v_out_index)
{
    ManagedConnection conn(*this);
    pqxx::work tx(*conn);

    pqxx::result result = tx.exec_params("SELECT * FROM transparent_outputs WHERE tx_id = $1 AND output_index = $2", txid, v_out_index);

    if (!result.empty())
    {
        return result[0];
    }

    return std::nullopt;
}

uint64_t Database::GetSyncedBlockCountFromDB()
{
    try
    {
        ManagedConnection conn(*this);
        pqxx::work tx(*conn);
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

        return syncedBlockCount;
    }
    catch (std::exception &e)
    {
        __ERROR__(e.what());
        return 0;
    }
    catch (pqxx::unexpected_rows &e)
    {
        __ERROR__(e.what());
        return 0;
    }
}

void Database::StorePeers(const Json::Value &peer_info)
{

    if (!peer_info.isNull())
    {

        try
        {
            ManagedConnection connection(*this);
            pqxx::work tx{*connection};

            tx.exec("TRUNCATE TABLE peerinfo;");

            connection->prepare("insert_peer_info", "INSERT INTO peerinfo (addr, lastsend, lastrecv, conntime, subver, synced_blocks) VALUES ($1, $2, $3, $4, $5, $6)");

            if (peer_info.isArray() && peer_info.size() > 0)
            {
                for (const Json::Value &peer : peer_info)
                {
                    tx.exec_prepared("insert_peer_info", peer["addr"].asString(), peer["lastsend"].asString(), peer["lastrecv"].asString(), peer["conntime"].asString(), peer["subver"].asString(), peer["synced_blocks"].asString());
                }

                tx.commit();
            }
        }
        catch (const std::exception &e)
        {
            __ERROR__(e.what());
        }
    }
}

void Database::StoreChainInfo(const Json::Value &chain_info)
{

    if (!chain_info.isNull())
    {
        std::string insert_chain_info_query{"INSERT INTO chain_info (orchard_pool_value, best_block_hash, size_on_disk, best_height, total_chain_value) VALUES ($1, $2, $3, $4, $5)"};

        double orchard_pool_value{0.0};
        Json::Value value_pools = chain_info["valuePools"];

        if (value_pools.isNull())
        {
            uint64_t total_chain_value = std::accumulate(value_pools.begin(), value_pools.end(), 0.0, [&orchard_pool_value](uint64_t accumulator, const Json::Value& pool)
                                                                   {
            if (pool["id"].asString() == "orchard")
            {
                orchard_pool_value = pool["chainValue"].asDouble();
            }

            return accumulator += pool["chainValue"].asDouble(); });

            try
            {
                ManagedConnection conn(*this);
                pqxx::work tx{*conn};
                tx.exec_params(insert_chain_info_query, orchard_pool_value, chain_info["bestblockhash"].asString(), chain_info["size_on_disk"].asDouble(), chain_info["estimatedheight"].asInt(), total_chain_value);
                tx.commit();
            }
            catch (const std::exception &e)
            {
                __ERROR__(e.what());
            }
        }
    }
}