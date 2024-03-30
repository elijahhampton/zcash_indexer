#include "database.h"
#include <fstream>
#include <string>
#include <iostream>
#include <memory>

const uint64_t Database::InvalidHeight;

bool Database::is_connected = false;
bool Database::is_database_setup = false;

std::queue<std::unique_ptr<pqxx::connection>> Database::connection_pool;
std::condition_variable Database::cv_connection_pool;
std::mutex Database::cs_connection_pool;

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
        cv_connection_pool.wait(lock, [this]
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

void Database::createBlocksTable() {
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
void Database::createTransactionsTable() {
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
void Database::createCheckpointsTable() {
    const std::string query = R"(
        CREATE TABLE checkpoints (
        chunk_start_height INTEGER PRIMARY KEY,
        chunk_end_height INTEGER,
        last_checkpoint INTEGER
        )
    )";

    ExecuteTableCreationQuery(query, "checkpoints");
}
void Database::createTransparentInputsTable() {
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
void Database::createTransparentOutputsTable() {
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

void Database::createPeerInfoTable() {
    const std::string query = R"(
        CREATE TABLE peerinfo (addr TEXT, lastsend TEXT, lastrecv TEXT, conntime TEXT, subver TEXT, synced_blocks TEXT)
        )";

    ExecuteTableCreationQuery(query, "peerinfo");
}

void Database::createChainInfoTable() {
    const std::string query = R"(
             CREATE TABLE chain_info (orchard_pool_value DOUBLE PRECISION, best_block_hash TEXT, size_on_disk DOUBLE PRECISION, best_height INT, total_chain_value DOUBLE PRECISION)
    )";

    ExecuteTableCreationQuery(query, "chain_info");
}

void Database::ExecuteTableCreationQuery(const std::string& query, const std::string& tableName)
{
    ManagedConnection conn(*this);
    pqxx::work tx(*conn);
    try
    {
        tx.exec(query);
        tx.commit();
    }
    catch (const pqxx::sql_error& e)
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

    createBlocksTable();
    createTransactionsTable();
    createTransparentInputsTable();
    createTransparentOutputsTable();
    createPeerInfoTable();
    createCheckpointsTable();
    createChainInfoTable();
    
    is_database_setup = true;
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
            WHERE chunk_start_height = )" + transaction.quote(chunkStartHeight)
        );

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
    catch (const std::exception& e)
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
        if (!result.empty()) {
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

void Database::RemoveMissedBlock(size_t blockHeight)
{
    spdlog::debug(("Recovered block at height " + std::to_string(blockHeight)).c_str());
}

const std::string Database::CreateTransactionIdSqlRepresentation(const Json::Value& transactions, int start, int end) const {
    if (start > end) {
        return "";
    }

    if (start == end) {
        if (transactions[start].isMember("txid") && transactions[start]["txid"].isString()) {
            return "\"" + transactions[start]["txid"].asString() + "\"";
        }
        return "";
    }

    int mid = start + (end - start) / 2;
    std::string leftHalf = CreateTransactionIdSqlRepresentation(transactions, start, mid);
    std::string rightHalf = CreateTransactionIdSqlRepresentation(transactions, mid + 1, end);

    if (!leftHalf.empty() && !rightHalf.empty()) {
        return leftHalf + "," + rightHalf;
    } else if (!leftHalf.empty()) {
        return leftHalf;
    } else {
        return rightHalf;
    }
}

void Database::StoreChunk(const std::vector<Json::Value> &chunk, uint64_t chunkStartHeight, uint64_t chunkEndHeight, uint64_t trueRangeStartHeight)
{
    spdlog::info("Syncing path: StoreChunk()");
    // Check the trueRangeStartHeight for this chunk in case this chunk is subchunk of another
    // chunk that was started in a previous session.
    std::optional<Database::Checkpoint> checkpointOpt = this->GetCheckpoint(trueRangeStartHeight);
    bool checkpointExist = checkpointOpt.has_value();

    ManagedConnection conn(*this);
    const char* insert_block_statement = R"(
        INSERT INTO blocks (hash, height, timestamp, nonce, size, num_transactions, total_block_output, difficulty, chainwork, merkle_root, version, bits, transaction_ids, num_outputs, num_inputs, total_block_input, miner)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17)
        ON CONFLICT (hash)
        DO NOTHING;
    )";

    conn->prepare("insert_block", insert_block_statement);

    pqxx::work insert_block_work(*conn);

    bool should_commit_block{true};
    auto timeSinceLastCheckpoint = std::chrono::steady_clock::now();
    size_t current_block_processing_index{static_cast<size_t>(chunkStartHeight)};

    for (const auto &item : chunk)
    {
        if (item == Json::nullValue)
        {
            this->AddMissedBlock(item["height"].asLargestInt());
            ++current_block_processing_index;
            continue;
        }

        try
        {
            // Extract the necessary values from the block
            const int numTxs = item["tx"].size();
            const std::string nonce = item["nonce"].asString();
            const int version = item["version"].asInt();
            const std::string prevBlockHash = item["previousblockhash"].asString();
            const std::string nextBlockHash = item["nextblockhash"].asString();
            std::string merkle_root = item["merkleroot"].asString();
            const int timestamp = item["time"].asInt();
            const int difficulty = item["difficulty"].asInt();
            const Json::Value transactions = item["tx"];
            const std::string hash = item["hash"].asString();
            const int height = item["height"].asLargestInt();
            const int size = item["size"].asInt();
            const std::string chainwork = item["chainwork"].asString();
            const std::string bits = item["bits"].asString();

            size_t num_outputs_in_block{0};
            size_t num_inputs_in_block{0};

            // Parse transaction ids into sql list representation
            std::string transaction_ids_sql_representation = "{";

            if (transactions.isArray() && numTxs > 0)
            {
                // Parse transaction ids into sql list representation
                transaction_ids_sql_representation += CreateTransactionIdSqlRepresentation(transactions, 0, numTxs - 1);

                // Accumulate number of transaction inputs and outputs
                num_outputs_in_block = std::accumulate(transactions.begin(), transactions.end(), 0, [](size_t sum, const Json::Value& tx) {
                    return sum + tx["vout"].size();
                });
                num_inputs_in_block = std::accumulate(transactions.begin(), transactions.end(), 0, [](size_t sum, const Json::Value& tx) {
                    return sum + tx["vin"].size();
                });
            }
            transaction_ids_sql_representation += "}";

            double total_block_public_output{0};
            double total_block_public_input{0};
            std::optional<pqxx::row> input_transaction_output;
            for (const Json::Value &tx : transactions)
            {
                // Sum the total block public output
                for (const Json::Value &voutItem : tx["vout"])
                {
                    total_block_public_output += voutItem["value"].asDouble();
                }

                // Sum the total block public input (0 is the transaction is coinbase)
                if (tx["vin"].size() <= 1)
                {
                    total_block_public_input = 0;
                }
                else
                {
                    total_block_public_input = std::accumulate(tx["vin"].begin(), tx["vin"].end(), 0.0, [this, &input_transaction_output](double sum, const Json::Value& vinItem) {
                        input_transaction_output = this->GetOutputByTransactionIdAndIndex(vinItem["txid"].asString(), static_cast<uint64_t>(vinItem["vout"].asInt()));
                        return sum + (input_transaction_output.has_value() ? input_transaction_output.value()["value"].as<double>() : 0.0);
                    });
                }
            }

            insert_block_work.exec_prepared("insert_block", hash, height, timestamp, nonce, size, numTxs, total_block_public_output, difficulty, chainwork, merkle_root, version, bits, transaction_ids_sql_representation, num_outputs_in_block, num_inputs_in_block, total_block_public_input, "");
            this->StoreTransactions(item, conn, insert_block_work);
            spdlog::debug(("Completed procesing for block with height: " + item["height"].toStyledString()).c_str());
        }
        catch (const std::exception &e)
        {
            spdlog::error(e.what());
            this->AddMissedBlock(item["height"].asLargestInt());
            should_commit_block = false;
        }

        // Commit the block before potentially taking a checkpoint
        if (should_commit_block)
        {
            insert_block_work.commit();
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsedTimeSinceLastCheckpoint = now - timeSinceLastCheckpoint;

        Database::Checkpoint checkpoint;
        if (checkpointExist)
        {
            checkpoint = checkpointOpt.value();

            if (elapsedTimeSinceLastCheckpoint >= std::chrono::minutes(1))
            {

                this->UpdateChunkCheckpoint(checkpointExist ? checkpoint.chunkStartHeight : chunkStartHeight, current_block_processing_index);
                timeSinceLastCheckpoint = now;
            }

            // Check one index before the end height to account for how the vector is indexed
            if (current_block_processing_index == chunkEndHeight)
            {
                this->UpdateChunkCheckpoint(checkpointExist ? checkpoint.chunkStartHeight : chunkStartHeight, current_block_processing_index);
                break;
            }
        }

        ++current_block_processing_index;
        should_commit_block = true;
    }

    conn->unprepare("insert_block");
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

void Database::StoreTransactions(const Json::Value &block, const ManagedConnection &conn, pqxx::work &blockTransaction)
{
    spdlog::info(("Storing transactions for height: " + block["height"].asString()).c_str());

    if (!block["tx"].isArray())
    {
        throw std::runtime_error("Database::StoreTransactions: Invalid input for transactions. Value is not an array.");
    }

    std::stringstream ss;
    ss << std::this_thread::get_id();
    std::string curr_thread_id = ss.str();

    std::string insert_transactions_prepare = curr_thread_id + "_insert_transactions";
    std::string insert_transparent_inputs_prepare = curr_thread_id + "_insert_transparent_inputs";
    std::string insert_transparent_outputs_prepare = curr_thread_id + "_insert_transparent_outputs";

    conn->prepare(insert_transactions_prepare,
                  R"(
        INSERT INTO transactions 
        (tx_id, size, is_overwintered, version, total_public_input, total_public_output, hex, hash, timestamp, height, num_inputs, num_outputs)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)
        ON CONFLICT (tx_id) 
        DO NOTHING
        )");

    conn->prepare(insert_transparent_outputs_prepare,
                  R"(
                 INSERT INTO transparent_outputs 
                 (tx_id, output_index, recipients, value)
                 VALUES ($1, $2, $3, $4)
              )");

    conn->prepare(insert_transparent_inputs_prepare,
                  R"(
                 INSERT INTO transparent_inputs 
                 (tx_id, vin_tx_id, v_out_idx, value, senders, coinbase)
                 VALUES ($1, $2, $3, $4, $5, $6)
                 ON CONFLICT (tx_id) 
                 DO NOTHING
              )");

    // Save transactions
    if (block["tx"].size() > 0)
    {
        bool isCoinbaseTransaction = false;
        std::string txid{""};
        int version;
        std::string prevBlockHash;
        std::string nextBlockHash;
        std::string merkle_root;
        int timestamp;
        std::string nonce;
        std::string hash;
        int height;
        Json::Value transactions;
        std::string size;
        std::string hex;
        std::string is_overwintered{"false"};
        double total_public_output{0.0};
        double total_public_input{0.0};
        uint32_t num_inputs_in_transaction{0};
        uint32_t num_outputs_in_transaction{0};

        for (const Json::Value &tx : block["tx"])
        {
            try
            {
                version = block["version"].asInt();
                prevBlockHash = block["previousblockhash"].asString();
                nextBlockHash = block["previousblockhash"].asString();
                merkle_root = block["merkleroot"].asString();
                timestamp = block["time"].asInt();
                std::string nonce = block["nonce"].asString();
                Json::Value transactions = tx;
                std::string hash = block["hash"].asString();
                height = block["height"].asLargestInt();
                size = tx["size"].asString();
                hex = tx["hex"].asString();
                is_overwintered = tx["overwintered"].asString();
                num_inputs_in_transaction = tx["vin"].size();
                num_outputs_in_transaction = tx["vout"].size();

                // Transaction id
                txid = tx["txid"].asString();

                // Transaction inputs
                if (tx["vin"].size() > 0)
                {
                    std::string vin_tx_id;
                    uint32_t v_out_idx;
                    std::string coinbase{""};
                    std::optional<pqxx::row> vin_transaction_look_buffer;
                    std::string senders{"{}"};

                    double current_input_value;

                    for (const Json::Value &input : tx["vin"])
                    {
                        try
                        {
                            if (input.isMember("coinbase"))
                            {
                                isCoinbaseTransaction = true;
                                coinbase = input["coinbase"].asString();
                                vin_tx_id = "-1";
                                v_out_idx = 0; // TODO: Find a better way to represent v_out_idx for coinbase transactions.
                                senders = "{}";
                            }
                            else
                            {
                                coinbase = "";
                                vin_tx_id = input["txid"].asString();
                                v_out_idx = input["vout"].asInt();

                                // Find the vout referenced in this vin to get the value and add to the total public input
                                vin_transaction_look_buffer = this->GetOutputByTransactionIdAndIndex(vin_tx_id, v_out_idx);
                                if (vin_transaction_look_buffer.has_value())
                                {
                                    pqxx::row output_specified_in_vin = vin_transaction_look_buffer.value();
                                    current_input_value = output_specified_in_vin["value"].as<double>();
                                    total_public_input += current_input_value;
                                    senders = output_specified_in_vin["recipients"].as<std::string>();
                                }
                                else
                                {
                                    senders = "{}";
                                    total_public_input += 0;
                                    current_input_value += 0;
                                }
                            }

                            blockTransaction.exec_prepared(insert_transparent_inputs_prepare, txid, vin_tx_id, v_out_idx, current_input_value, senders, coinbase);
                            senders = "{}";
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
                }

                double currentOutputValue{0.0};

                // Transaction outputs
                if (tx["vout"].size() > 0)
                {
                    size_t outputIndex{0};
                    std::vector<std::string> recipients;
                    for (const Json::Value &vOutEntry : tx["vout"])
                    {
                        outputIndex = vOutEntry["n"].asLargestInt();
                        currentOutputValue = vOutEntry["value"].asDouble();
                        total_public_output += currentOutputValue;

                        try
                        {
                            Json::Value vOutAddresses = vOutEntry["scriptPubKey"]["addresses"];
                            std::string recipientList = "{";
                            if (vOutAddresses.isArray() && vOutAddresses.size() > 0)
                            {
                                for (const Json::Value &vOutAddress : vOutAddresses)
                                {
                                    recipients.push_back(vOutAddress.asString());
                                }

                                if (!recipients.empty())
                                {
                                    recipientList += "\"" + recipients[0] + "\"";
                                    for (size_t i = 1; i < recipients.size(); ++i)
                                    {
                                        recipientList += ",\"" + recipients[i] + "\"";
                                    }
                                }
                            }

                            recipientList += "}";

                            blockTransaction.exec_prepared(insert_transparent_outputs_prepare, txid, outputIndex, recipientList, currentOutputValue);
                            recipients.clear();
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
                }

                blockTransaction.exec_prepared(insert_transactions_prepare, txid, size, is_overwintered, version, total_public_input, total_public_output, hex, hash, std::to_string(timestamp), height, num_inputs_in_transaction, num_outputs_in_transaction);

                total_public_input = 0;
                total_public_output = 0;
            }
            catch (const pqxx::sql_error &e)
            {
                spdlog::error(e.what());
                conn->unprepare(insert_transactions_prepare);
                conn->unprepare(insert_transparent_inputs_prepare);
                conn->unprepare(insert_transparent_outputs_prepare);
                throw;
            }
            catch (const std::exception &e)
            {
                spdlog::error(e.what());
                conn->unprepare(insert_transactions_prepare);
                conn->unprepare(insert_transparent_inputs_prepare);
                conn->unprepare(insert_transparent_outputs_prepare);
                throw;
            }

            // Reset local variables for the next transaction
            isCoinbaseTransaction = false;
        }
    }

    conn->unprepare(insert_transactions_prepare);
    conn->unprepare(insert_transparent_inputs_prepare);
    conn->unprepare(insert_transparent_outputs_prepare);
}

uint64_t Database::GetSyncedBlockCountFromDB()
{
    ManagedConnection conn(*this);

    try
    {
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

    try
    {
        if (peer_info.isNull())
        {
            return;
        }

        pqxx::work tx{*connection};

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
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
    }
}

void Database::StoreChainInfo(const Json::Value &chain_info)
{
    ManagedConnection conn(*this);

    if (!chain_info.isNull())
    {
        const char *insert_chain_info_query{"INSERT INTO chain_info (orchard_pool_value, best_block_hash, size_on_disk, best_height, total_chain_value) VALUES ($1, $2, $3, $4, $5)"};

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
            pqxx::work tx{*conn};
            tx.exec_params(insert_chain_info_query, orchardPoolValue, chain_info["bestblockhash"].asString(), chain_info["size_on_disk"].asDouble(), chain_info["estimatedheight"].asInt(), totalChainValueAccumulator);
            tx.commit();
            delete insert_chain_info_query;
        }
        catch (const std::exception &e)
        {
            spdlog::error(e.what());
            delete insert_chain_info_query;
        }
    }
}