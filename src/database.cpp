#include "database.h"
#include <fstream>
#include <string>
#include <iostream>
#include <memory>

std::mutex Database::databaseConnectionCloseMutex;
std::condition_variable Database::databaseConnectionCloseCondition;

const uint64_t Database::InvalidHeight;

Database::~Database()
{
    this->ShutdownConnections();
}

bool Database::Connect(size_t poolSize, const std::string &conn_str)
{
    if (this->is_connected) {
        std::cerr << "Database is already connected. Exiting function Database::Connect()" << std::endl;
        return false;
    }

    try
    {
        for (size_t i = 0; i < poolSize; ++i)
        {
            auto conn = std::make_unique<pqxx::connection>(conn_str);
            connectionPool.push(std::move(conn));
        }

        this->is_connected = true;

        return true;
    }
    catch (std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        this->is_connected = false;
        throw std::runtime_error("Error occurred while creating connection pool. Database unable to connect.");
    }

    return false;
}

// TODO: Count number of databases used by each thread
std::unique_ptr<pqxx::connection> Database::GetConnection()
{
    try
    {
        std::unique_lock<std::mutex> lock(poolMutex);
        poolCondition.wait(lock, [this]
                           { return !connectionPool.empty(); });

        auto conn = std::move(connectionPool.front());
        connectionPool.pop();
        std::cout << "GetConnection(). poolSize=" << connectionPool.size() << std::endl;

        if (conn == nullptr || !conn->is_open())
        {
            throw std::runtime_error("------Invalid connection: connection is null or not open.--------");
        }

        return conn;
    }
    catch (std::exception &e)
    {
        return nullptr;
    }

    return nullptr;
}

void Database::ReleaseConnection(std::unique_ptr<pqxx::connection> conn)
{
    try
    {
        if (conn == nullptr || !conn->is_open())
        {
            // Optionally handle invalid connection here
            std::cout << "ReleaseConnection: Invalid connection. Connection is null or not open." << std::endl;
            return;
        }

        std::lock_guard<std::mutex> lock(poolMutex);
        connectionPool.push(std::move(conn));
        poolCondition.notify_one();
        std::cout << "ReleaseConnection: Connection released. poolSize=" << connectionPool.size() << std::endl;
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Failed to release a database connection.");
    }
}

void Database::ShutdownConnections()
{
    // TODO
}

void Database::CreateTables()
{
    if (this->is_database_setup) {
        std::cerr << "Database has already been setup in the current instance. Exiting function Database::CreateTables()" << std::endl;
        return;
    }

    std::unique_ptr<pqxx::connection> conn = this->GetConnection();

    // std::array of statements to create the required postgres tables
    // Tables: blocks, transaction, checkpoints, transparent_inputs, transparent_outputs, peerinfo
    std::array<std::string, 7> createTableStatements{"CREATE TABLE blocks ("
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

    // TODO: Implement batch insertion
    try
    {
        for (const std::string &query : createTableStatements)
        {
            try
            {
                pqxx::work w(*conn.get());
                w.exec(query);
                w.commit();
            }
            catch (const pqxx::sql_error &e)
            {
                std::cout << e.what() << std::endl;
                if (e.sqlstate() == "42P07")
                {
                    std::cerr << "Table already exists: " << e.what() << std::endl;
                    continue;
                }
                else
                {
                    this->is_database_setup = false;
                    throw;
                }
            }
        }

        this->ReleaseConnection(std::move(conn));
    }
    catch (const pqxx::sql_error &e)
    {
        this->ReleaseConnection(std::move(conn));
        throw;
    }
    catch (const std::exception &e)
    {
        this->ReleaseConnection(std::move(conn));
        throw;
    }
}

void Database::UpdateChunkCheckpoint(size_t chunkStartHeight, size_t currentProcessingChunkHeight)
{
    std::cout << "Updating check point: [" << chunkStartHeight << "," << currentProcessingChunkHeight << "]" << std::endl;

    // TODO: Check if checkpoint exist and throw a runtime error if it doesn't
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();

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

        conn->unprepare("update_checkpoint");
        this->ReleaseConnection(std::move(conn));
    }
    catch (std::exception &e)
    {
        this->ReleaseConnection(std::move(conn));
    }
}

std::optional<Database::Checkpoint> Database::GetCheckpoint(signed int chunkStartHeight)
{
    if (chunkStartHeight == Database::InvalidHeight)
    {
        return std::nullopt;
    }

    std::unique_ptr<pqxx::connection> conn = this->GetConnection();

    try
    {

        pqxx::work transaction(*conn.get());

        pqxx::result result = transaction.exec(
            "SELECT chunk_start_height, chunk_end_height, last_checkpoint "
            "FROM checkpoints WHERE chunk_start_height = " +
            transaction.quote(chunkStartHeight));

        if (result.empty())
        {
            this->ReleaseConnection(std::move(conn));
            return std::nullopt;
        }
        else
        {
            pqxx::row row = result[0];

            Checkpoint checkpoint;

            checkpoint.chunkStartHeight = row["chunk_start_height"].as<size_t>();
            checkpoint.chunkEndHeight = row["chunk_end_height"].as<size_t>();
            checkpoint.lastCheckpoint = row["last_checkpoint"].as<size_t>();

            this->ReleaseConnection(std::move(conn));
            return checkpoint;
        }
    }
    catch (const pqxx::sql_error &e)
    {
        std::cerr << "SQL error: " << e.what() << std::endl;
        std::cerr << "Query was: " << e.query() << std::endl;
        this->ReleaseConnection(std::move(conn));
        throw;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        this->ReleaseConnection(std::move(conn));
        throw;
    }
}

void Database::CreateCheckpointIfNonExistent(size_t chunkStartHeight, size_t chunkEndHeight)
{
    try
    {
        std::cout << "Creating chunk checkpoint: [ " << chunkStartHeight << " - " << chunkEndHeight << " ]" << std::endl;

        std::string insertCheckpointStatement = R"(
    INSERT INTO checkpoints (
        chunk_start_height, 
        chunk_end_height, 
        last_checkpoint
    ) VALUES ($1, $2, $3);
)";

        std::unique_ptr<pqxx::connection> conn = this->GetConnection();
        conn->prepare("insert_checkpoint", insertCheckpointStatement);
        pqxx::work transaction(*conn.get());

        transaction.exec_prepared("insert_checkpoint", chunkStartHeight, chunkEndHeight, chunkStartHeight);
        transaction.commit();

        conn->unprepare("insert_checkpoint");

        this->ReleaseConnection(std::move(conn));
    }
    catch (std::exception &e)
    {

    }
}

std::stack<Database::Checkpoint> Database::GetUnfinishedCheckpoints()
{
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();

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

        for (auto row : result)
        {
            currentCheckpoint.chunkStartHeight = row["chunk_start_height"].as<size_t>();
            currentCheckpoint.chunkEndHeight = row["chunk_end_height"].as<size_t>();
            currentCheckpoint.lastCheckpoint = row["last_checkpoint"].as<size_t>();
            checkpoints.push(currentCheckpoint);
        }

        this->ReleaseConnection(std::move(conn));

        // Return the found checkpoints
        return checkpoints;
    }
    catch (const pqxx::sql_error &e)
    {
        this->ReleaseConnection(std::move(conn));
    }
    catch (const std::exception &e)
    {
        this->ReleaseConnection(std::move(conn));
    }
}

void Database::AddMissedBlock(size_t blockHeight)
{
}

void Database::RemoveMissedBlock(size_t blockHeight)
{
}

void Database::StoreChunk(bool isTrackingCheckpointForChunk, const std::vector<Json::Value> &chunk, uint64_t chunkStartHeight, uint64_t chunkEndHeight, uint64_t trueRangeStartHeight)
{
    std::cout << "StoreChunk("
              << "ChunkStart=" << chunkStartHeight << " "
              << "ChunkEnd=" << chunkEndHeight << " "
              << "TrueRange=" << trueRangeStartHeight << std::endl;

    std::optional<Database::Checkpoint> checkpointOpt = this->GetCheckpoint(trueRangeStartHeight);
    bool checkpointExist = checkpointOpt.has_value();
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();

    conn->prepare("insert_block",
                  "INSERT INTO blocks (hash, height, timestamp, nonce, size, num_transactions, total_block_output, difficulty, chainwork, merkle_root, version, bits, transaction_ids, num_outputs, num_inputs, total_block_input, miner) "
                  "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17) "
                  "ON CONFLICT (hash) "
                  "DO NOTHING;");

    pqxx::work insertBlockWork(*conn.get());
    bool shouldCommitBlock{true};
    auto timeSinceLastCheckpoint = std::chrono::steady_clock::now();
    size_t chunkCurrentProcessingIndex{static_cast<size_t>(chunkStartHeight)};

    for (const auto &item : chunk)
    {
        // Check for null Json::Value in block chunk
        if (item == Json::nullValue)
        {
            // Mark as missed and add to missed blocks
            this->AddMissedBlock(item["height"].asLargestInt());
            ++chunkCurrentProcessingIndex;
            continue;
        }

        try
        {
            // Parse Block Header
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
            const int numTxs = item["tx"].size();
            const std::string chainwork = item["chainwork"].asString();
            const std::string bits = item["bits"].asString();

            size_t num_outputs_in_block{0};
            size_t num_inputs_in_block{0};

            // Parse transaction ids into sql list representation
            std::string transaction_ids_sql_representation = "{";
            if (transactions.isArray() && transactions.size() > 0)
            {
                for (int i = 0; i < transactions.size(); ++i)
                {
                    num_outputs_in_block += transactions[i]["vout"].size();
                    num_inputs_in_block += transactions[i]["vin"].size();

                    if (transactions[i].isMember("txid") && transactions[i]["txid"].isString())
                    {
                        std::string txid = transactions[i]["txid"].asString();
                        transaction_ids_sql_representation += "\"" + txid + "\"";
                        if (i < transactions.size() - 1)
                        {
                            transaction_ids_sql_representation += ",";
                        }
                    }
                }
            }
            transaction_ids_sql_representation += "}";

            // Calculate block public output
            double total_block_public_output{0};
            double total_block_public_input{0};
            std::optional<pqxx::row> vin_transaction_look_buffer;
            for (const Json::Value tx : transactions)
            {
                for (const Json::Value &voutItem : tx["vout"])
                {
                    total_block_public_output += voutItem["value"].asDouble();
                }

                if (tx["vin"].size() == 1)
                {
                    total_block_public_input = 0;
                }
                else
                {
                    Json::Value output_specified_in_vin;
                    for (const Json::Value &vinItem : tx["vin"])
                    {
                        vin_transaction_look_buffer = this->GetOutputByTransactionIdAndIndex(vinItem["txid"].asString(), static_cast<uint64_t>(vinItem["vout"].asInt()));
                        if (vin_transaction_look_buffer.has_value())
                        {
                            pqxx::row output_specified_in_vin = vin_transaction_look_buffer.value();
                            total_block_public_input += output_specified_in_vin["value"].as<double>();
                        }
                        else
                        {
                            total_block_public_input += 0;
                        }

                        output_specified_in_vin = 0;
                    }
                }
            }

            insertBlockWork.exec_prepared("insert_block", hash, height, timestamp, nonce, size, numTxs, total_block_public_output, difficulty, chainwork, merkle_root, version, bits, transaction_ids_sql_representation, num_outputs_in_block, num_inputs_in_block, total_block_public_input, "");
            this->StoreTransactions(item, conn, insertBlockWork);
        }
        catch (const pqxx::sql_error &e)
        {
            if (e.sqlstate().find("duplicate key value violates unique constraint") != std::string::npos)
            {
            }
            else
            {
            }

            this->AddMissedBlock(item["height"].asLargestInt());
            shouldCommitBlock = false;
        }
        catch (const std::exception &e)
        {
            this->AddMissedBlock(item["height"].asLargestInt());
            shouldCommitBlock = false;
        }

        // Commit the block before taking a checkpoint
        if (shouldCommitBlock)
        {
            std::cout << "Commiting block" << std::endl;
            insertBlockWork.commit();
        }
        if (isTrackingCheckpointForChunk)
        {
            // TODO: If the checkpont doesn't exist the update chunk checkpoint function shouldn't update it.. throw error
            auto now = std::chrono::steady_clock::now();
            auto elapsedTimeSinceLastCheckpoint = now - timeSinceLastCheckpoint;

            Database::Checkpoint checkpoint;
            if (checkpointExist)
            {
                checkpoint = checkpointOpt.value();

                if (elapsedTimeSinceLastCheckpoint >= std::chrono::seconds(10))
                {

                    this->UpdateChunkCheckpoint(checkpointExist ? checkpoint.chunkStartHeight : chunkStartHeight, chunkCurrentProcessingIndex);
                    timeSinceLastCheckpoint = now;
                }

                // Check one index before the end height to account for how the vector is indexed
                if (chunkCurrentProcessingIndex == chunkEndHeight)
                {
                    this->UpdateChunkCheckpoint(checkpointExist ? checkpoint.chunkStartHeight : chunkStartHeight, chunkCurrentProcessingIndex);
                    break;
                }
            }
        }

        ++chunkCurrentProcessingIndex;
        shouldCommitBlock = true;
    }

    conn->unprepare("insert_block");
    this->ReleaseConnection(std::move(conn));
}

std::optional<pqxx::row> Database::GetTransactionById(const std::string &txid)
{
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();

    pqxx::work tx(*conn.get());

    pqxx::result result = tx.exec_params(
        "SELECT * FROM transactions WHERE tx_id = $1", txid);

    this->ReleaseConnection(std::move(conn));

    if (!result.empty())
    {
        return result[0];
    }

    return std::nullopt;
}

std::optional<pqxx::row> Database::GetOutputByTransactionIdAndIndex(const std::string &txid, uint64_t v_out_index)
{
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();
    pqxx::work tx(*conn.get());

    pqxx::result result = tx.exec_params("SELECT * FROM transparent_outputs WHERE tx_id = $1 AND output_index = $2", txid, v_out_index);

    this->ReleaseConnection(std::move(conn));

    if (!result.empty())
    {
        return result[0];
    }

    return std::nullopt;
}

void Database::StoreTransactions(const Json::Value &block, const std::unique_ptr<pqxx::connection> &conn, pqxx::work &blockTransaction)
{
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
                        std::cout << "Adding to total public output: " << currentOutputValue << std::endl;
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
                            throw;
                        }
                        catch (const std::exception &e)
                        {
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
                conn->unprepare(insert_transactions_prepare);
                conn->unprepare(insert_transparent_inputs_prepare);
                conn->unprepare(insert_transparent_outputs_prepare);
                throw;
            }
            catch (const std::exception &e)
            {
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
    std::unique_ptr<pqxx::connection> conn = this->GetConnection();

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

        this->ReleaseConnection(std::move(conn));
        return syncedBlockCount;
    }
    catch (std::exception &e)
    {
        this->ReleaseConnection(std::move(conn));
        return 0;
    }
    catch (pqxx::unexpected_rows &e)
    {
        this->ReleaseConnection(std::move(conn));
        return 0;
    }
}

void Database::StorePeers(const Json::Value &peer_info)
{
    std::unique_ptr<pqxx::connection> connection = this->GetConnection();
    try
    {

        if (peer_info.isNull())
        {
            this->ReleaseConnection(std::move(connection));
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

        this->ReleaseConnection(std::move(connection));
    }
    catch (const std::exception &e)
    {
        this->ReleaseConnection(std::move(connection));
    }
}

void Database::StoreChainInfo(const Json::Value& chain_info)
{

    if (!chain_info.isNull()) {
        const char * insert_chain_info_query{"INSERT INTO chain_info (orchard_pool_value, best_block_hash, size_on_disk, best_height, total_chain_value) VALUES ($1, $2, $3, $4, $5)"};
        auto conn = this->GetConnection();

        double orchardPoolValue{0.0};
        Json::Value valuePools = chain_info["valuePools"];
        double totalChainValueAccumulator{0.0};
        if (!valuePools.isNull()) {
            for (const Json::Value& pool : valuePools)
            {
                if (pool["id"].asString() == "orchard")
                {
                    orchardPoolValue = pool["chainValue"].asDouble();
                }

                totalChainValueAccumulator += pool["chainValue"].asDouble();
            }
        }

        try {
            pqxx::work tx{*conn.get()};
            tx.exec_params(insert_chain_info_query, orchardPoolValue, chain_info["bestblockhash"].asString(), chain_info["size_on_disk"].asDouble(), chain_info["estimatedheight"].asInt(), totalChainValueAccumulator);
            tx.commit();
            this->ReleaseConnection(std::move(conn));
        } catch(const std::exception& e) {
            this->ReleaseConnection(std::move(conn));
        }
    }
}