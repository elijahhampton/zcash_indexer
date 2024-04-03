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
    spdlog::info("Attempting to connect to database and initialize connection pool.");

    if (is_connected)
    {

        spdlog::info("Database is already connected.");
        return;
    }

    spdlog::debug(("Initializing database pool with " + std::to_string(poolSize) + " connections").c_str());

    try
    {
        for (size_t i = 0; i < poolSize; ++i)
        {
            auto conn = std::make_unique<pqxx::connection>(conn_str);

            connection_pool.push(std::move(conn));
            spdlog::debug(("Completed connections " + std::to_string((i + 1)) + "/" + std::to_string(poolSize)).c_str());
        }

        is_connected = true;
    }
    catch (std::exception &e)
    {
        is_connected = false;
        ShutdownConnections();

        spdlog::error(e.what());
    }
}

std::unique_ptr<pqxx::connection> Database::GetConnection()
{
    spdlog::info("GetConnection()");
    try
    {
        std::unique_lock<std::mutex> lock(cs_connection_pool);
        cv_connection_pool.wait(lock, []
                                { return !connection_pool.empty(); });

        auto conn = std::move(connection_pool.front());
        connection_pool.pop();

        if (conn == nullptr || !conn->is_open())
        {
            spdlog::error("------Invalid connection: connection is null or not open.--------");
            throw std::runtime_error("------Invalid connection: connection is null or not open.--------");
        }

        return conn;
    }
    catch (std::exception &e)
    {
        spdlog::error(e.what());
        throw std::runtime_error(e.what());
    }
}

void Database::ReleaseConnection(std::unique_ptr<pqxx::connection> conn)
{
    spdlog::info("ReleaseConnection()");

    try
    {
        if (conn == nullptr || !conn->is_open())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(Database::cs_connection_pool);
        connection_pool.push(std::move(conn));
        cv_connection_pool.notify_one();
    }
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
        throw std::runtime_error(e.what());
    }
}

void Database::ShutdownConnections()
{
    std::lock_guard<std::mutex> lock(cs_connection_pool);
    if (!connection_pool.empty())
    {
        auto conn = std::move(connection_pool.front());
        connection_pool.pop();
    }
}

void Database::CreateBlocksTable()
{
    const std::string query = R"(
        CREATE TABLE blocks (
            hash TEXT PRIMARY KEY,
            height INTEGER,
            timestamp INTEGER,
            nonce TEXT,
            size INTEGER,
            num_transactions INTEGER,
            total_block_output DOUBLE PRECISION,
            difficulty DOUBLE PRECISION,
            chainwork TEXT,
            merkle_root TEXT,
            version INTEGER,
            bits TEXT,
            transaction_ids TEXT[],
            num_outputs INTEGER,
            num_inputs INTEGER,
            total_block_input DOUBLE PRECISION,
            miner TEXT
        )
    )";

    ExecuteTableCreationQuery(query, "blocks");
}
void Database::CreateTransactionsTable()
{
    const std::string query = R"(
        CREATE TABLE transactions (
        tx_id TEXT PRIMARY KEY,
        size INTEGER,
        is_overwintered TEXT,
        version INTEGER,
        total_public_input TEXT,
        total_public_output TEXT,
        hex TEXT,
        hash TEXT,
        timestamp INTEGER,
        height INTEGER,
        num_inputs INTEGER,
        num_outputs INTEGER
        )";

    ExecuteTableCreationQuery(query, "transactions");
}
void Database::CreateCheckpointsTable()
{
    const std::string query = R"(
        CREATE TABLE checkpoints (
        chunk_start_height INTEGER PRIMARY KEY,
        chunk_end_height INTEGER,
        last_checkpoint INTEGER
        )
    )";

    ExecuteTableCreationQuery(query, "checkpoints");
}
void Database::CreateTransparentInputsTable()
{
    const std::string query = R"(
        CREATE TABLE transparent_inputs (
        tx_id TEXT PRIMARY KEY,
        vin_tx_id TEXT,
        v_out_idx INTEGER,
        value DOUBLE PRECISION,
        senders TEXT[],
        coinbase TEXT
        )
    )";

    ExecuteTableCreationQuery(query, "transparent_inputs");
}
void Database::CreateTransparentOutputsTable()
{
    const std::string query = R"(
        CREATE TABLE transparent_outputs (
        tx_id TEXT,
        output_index INTEGER,
        recipients TEXT[],
        value TEXT
        )
    )";

    ExecuteTableCreationQuery(query, "transparent_outputs");
}

void Database::CreatePeerInfoTable()
{
    const std::string query = R"(
        CREATE TABLE peerinfo (addr TEXT, lastsend TEXT, lastrecv TEXT, conntime TEXT, subver TEXT, synced_blocks TEXT)
        )";

    ExecuteTableCreationQuery(query, "peerinfo");
}

void Database::CreateChainInfoTable()
{
    const std::string query = R"(
             CREATE TABLE chain_info (orchard_pool_value DOUBLE PRECISION, best_block_hash TEXT, size_on_disk DOUBLE PRECISION, best_height INT, total_chain_value DOUBLE PRECISION)
    )";

    ExecuteTableCreationQuery(query, "chain_info");
}

void Database::ExecuteTableCreationQuery(const std::string &query, const std::string &tableName)
{
    ManagedConnection conn(*this);
    pqxx::work tx(*conn);
    try
    {
        tx.exec(query);
        tx.commit();
    }
    catch (const pqxx::sql_error &e)
    {
        // SQL Error Codes: https://www.postgresql.org/docs/15/errcodes-appendix.html
        // Syncing might be picked up from another session. It is okay if a table already exists.
        if (e.sqlstate() == "42P07" || e.sqlstate() == "25P02")
        {
            spdlog::info("Table already exists. Skipping creation.");
        }
        else
        {
            spdlog::info("Error creating table. Aborting transaction.");
            tx.abort();
        }
    }
}

void Database::CreateTables()
{
    if (is_database_setup)
    {
        std::cerr << "Database has already been setup in the current instance. Exiting function Database::CreateTables()" << std::endl;
        return;
    }

    CreateBlocksTable();
    CreateTransactionsTable();
    CreateTransparentInputsTable();
    CreateTransparentOutputsTable();
    CreatePeerInfoTable();
    CreateCheckpointsTable();
    CreateChainInfoTable();

    is_database_setup = true;
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
    const std::string update_checkpoint_statement = R"(
         UPDATE checkpoints SET last_checkpoint = $2 WHERE chunk_start_height = $1
    )";

    try
    {
        ManagedConnection conn(*this);
        pqxx::work transaction(*conn);
        conn->prepare("update_checkpoint", update_checkpoint_statement);
        transaction.exec_prepared("update_checkpoint", chunkStartHeight, currentProcessingChunkHeight);
        transaction.commit();

        std::string message = "Updating checkpoint from start value " + std::to_string(chunkStartHeight) + " to " + std::to_string(currentProcessingChunkHeight);
        spdlog::debug(message.c_str());

        conn->unprepare("update_checkpoint");
    }
    catch (std::exception &e)
    {
        spdlog::error(e.what());
        throw;
    }
}

std::optional<Database::Checkpoint> Database::GetCheckpoint(signed int chunkStartHeight)
{
    if (chunkStartHeight == Database::InvalidHeight)
    {
        return std::nullopt;
    }
    try
    {
        ManagedConnection conn(*this);
        pqxx::work transaction(*conn);
        pqxx::result result = transaction.exec(
            R"(
            SELECT chunk_start_height, chunk_end_height, last_checkpoint
            FROM checkpoints
            WHERE chunk_start_height = )" +
            transaction.quote(chunkStartHeight));

        if (result.empty())
        {
            return std::nullopt;
        }

        pqxx::row row = result[0];
        Checkpoint checkpoint;
        checkpoint.chunkStartHeight = row["chunk_start_height"].as<size_t>();
        checkpoint.chunkEndHeight = row["chunk_end_height"].as<size_t>();
        checkpoint.lastCheckpoint = row["last_checkpoint"].as<size_t>();

        return checkpoint;
    }
    catch (const pqxx::sql_error &e)
    {
        spdlog::error(e.what());
        throw;
    }
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
        throw;
    }
}

void Database::CreateCheckpointIfNonExistent(size_t chunkStartHeight, size_t chunkEndHeight)
{
    try
    {
        ManagedConnection conn(*this);
        const std::string insert_checkpoint_statement = R"(
            INSERT INTO checkpoints (chunk_start_height, chunk_end_height, last_checkpoint) VALUES ($1, $2, $3);
        )";

        conn->prepare("insert_checkpoint", insert_checkpoint_statement);
        pqxx::work transaction(*conn);

        transaction.exec_prepared("insert_checkpoint", chunkStartHeight, chunkEndHeight, chunkStartHeight);
        transaction.commit();

        spdlog::debug(("Checkpoint created at height " + std::to_string(chunkStartHeight) + " to " + std::to_string(chunkEndHeight)).c_str());
        conn->unprepare("insert_checkpoint");
    }
    catch (std::exception &e)
    {
        spdlog::error(e.what());
    }
}

std::stack<Database::Checkpoint> Database::GetUnfinishedCheckpoints()
{
    std::stack<Checkpoint> checkpoints;

    try
    {
        ManagedConnection conn(*this);
        pqxx::work transaction(*conn);

        const std::string query = R"(
            SELECT chunk_start_height, chunk_end_height, last_checkpoint
            FROM checkpoints
            WHERE chunk_end_height != last_checkpoint
        )";

        // Execute query
        pqxx::result result = transaction.exec(query);

        // Process the sql rows for each checkpoint
        if (!result.empty())
        {
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
        }
    }
    catch (const pqxx::sql_error &e)
    {
        spdlog::error(e.what());
        throw std::runtime_error(e.what());
    }
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
        throw std::runtime_error(e.what());
    }

    return checkpoints;
}

void Database::AddMissedBlock(size_t blockHeight)
{
    spdlog::debug(("Missed block at height " + std::to_string(blockHeight)).c_str());
}

void Database::BatchStoreBlocks(std::vector<Block> &chunk, uint64_t chunkStartHeight, uint64_t chunkEndHeight, uint64_t trueRangeStartHeight)
{
    spdlog::info("Syncing path: BatchStoreBlocks()");

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
            spdlog::error(e.what());

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
    ManagedConnection conn(*this);

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

        spdlog::debug(("New synced block count " + std::to_string(syncedBlockCount)).c_str());

        return syncedBlockCount;
    }
    catch (std::exception &e)
    {
        spdlog::error(e.what());
        return 0;
    }
    catch (pqxx::unexpected_rows &e)
    {
        spdlog::error(e.what());
        return 0;
    }
}

void Database::StorePeers(const Json::Value &peer_info)
{
    ManagedConnection connection(*this);
    pqxx::work tx{*connection};
    try
    {
        if (!peer_info.isNull())
        {
            // Clear existing peers
            tx.exec("TRUNCATE TABLE peerinfo;");
            tx.commit();

            connection->prepare("insert_peer_info", R"(INSERT INTO peerinfo (addr, lastsend, lastrecv, conntime, subver, synced_blocks) VALUES ($1, $2, $3, $4, $5, $6))");

            if (peer_info.isArray() && peer_info.size() > 0)
            {
                for (const Json::Value &peer : peer_info)
                {
                    tx.exec_prepared("insert_peer_info", peer["addr"].asString(), peer["lastsend"].asString(), peer["lastrecv"].asString(), peer["conntime"].asString(), peer["subver"].asString(), peer["synced_blocks"].asString());
                }

                tx.commit();
            }
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
        tx.abort();
    }
}

void Database::StoreChainInfo(const Json::Value &chain_info)
{

    if (!chain_info.isNull())
    {
        const char *insertChainInfoStatement{"INSERT INTO chain_info (orchard_pool_value, best_block_hash, size_on_disk, best_height, total_chain_value) VALUES ($1, $2, $3, $4, $5)"};

        double orchardPoolValue{0.0};
        Json::Value valuePools = chain_info["valuePools"];
        double totalChainValue{0.0};
        if (!valuePools.isNull())
        {
            for (const Json::Value &pool : valuePools)
            {
                if (pool["id"].asString() == "orchard")
                {
                    orchardPoolValue = pool["chainValue"].asDouble();
                }

                totalChainValue += pool["chainValue"].asDouble();
            }

            ManagedConnection conn(*this);
            pqxx::work tx{*conn};

            try
            {
                tx.exec_params(insertChainInfoStatement, orchardPoolValue, chain_info["bestblockhash"].asString(), chain_info["size_on_disk"].asDouble(), chain_info["estimatedheight"].asInt(), totalChainValue);
                tx.commit();
            }
            catch (const std::exception &e)
            {
                spdlog::error(e.what());
                tx.abort();
            }
        }
    }
}