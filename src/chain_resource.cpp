#include "chain_resource.h"
#include "database.h"

StoreableBlockData::StoreableBlockData() : isValid(false) {}

StoreableBlockData::StoreableBlockData(const Block &block)
{
    if (block.isValid())
    {
        this->isValid = true;
        const Json::Value &rawBlock = block.GetRawJson();

        this->nonce = rawBlock["nonce"].asCString();
        this->version = rawBlock["version"].asUInt64();
        this->prev_block_hash = rawBlock["previousblockhash"].asCString();
        this->next_block_hash = rawBlock["nextblockhash"].asCString();
        this->merkle_root = rawBlock["merkleroot"].asCString();
        this->timestamp = rawBlock["time"].asUInt64();
        this->difficulty = rawBlock["difficulty"].asUInt64();
        this->transactions = rawBlock["tx"];
        this->hash = rawBlock["hash"].asCString();
        this->height = rawBlock["height"].asUInt64();
        this->size = rawBlock["size"].asUInt64();
        this->chainwork = rawBlock["chainwork"].asCString();
        this->bits = rawBlock["bits"].asCString();
        this->num_transactions = this->transactions.size();
    }
}

StoreableBlockData::StoreableBlockData(Json::Value rawBlock) : nonce(rawBlock["nonce"].asCString()),
                                                               version(rawBlock["version"].asUInt64()),
                                                               prev_block_hash(rawBlock["previousblockhash"].asCString()),
                                                               next_block_hash(rawBlock["nextblockhash"].asCString()),
                                                               merkle_root(rawBlock["merkleroot"].asCString()),
                                                               timestamp(rawBlock["time"].asUInt64()),
                                                               difficulty(rawBlock["difficulty"].asUInt64()),
                                                               transactions(rawBlock["tx"]),
                                                               num_transactions(this->transactions.size()),
                                                               hash(rawBlock["hash"].asCString()),
                                                               height(rawBlock["height"].asUInt64()),
                                                               size(rawBlock["size"].asUInt64()),
                                                               chainwork(rawBlock["chainwork"].asCString()),
                                                               bits(rawBlock["bits"].asCString())
{
    if (rawBlock.isNull() || !rawBlock.isObject())
    {
        this->isValid = false;
    }

    this->isValid = true;
}

void StoreableBlockData::ProcessBlockToStoreable(pqxx::work &blockTransaction, std::unique_ptr<pqxx::connection> &conn)
{
    conn->prepare("insert_block",
                  "INSERT INTO blocks (hash, height, timestamp, nonce, size, num_transactions, total_block_output, difficulty, chainwork, merkle_root, version, bits, transaction_ids, num_outputs, num_inputs, total_block_input, miner) "
                  "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17) "
                  "ON CONFLICT (hash) "
                  "DO NOTHING;");

    conn->prepare("insert_transactions",
                  R"(
        INSERT INTO transactions 
        (tx_id, size, is_overwintered, version, total_public_input, total_public_output, hex, hash, timestamp, height, num_inputs, num_outputs)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)
        ON CONFLICT (tx_id) 
        DO NOTHING
        )");

    try
    {
        const uint64_t transactions_size = this->transactions.size();

        if (this->transactions.isArray() && transactions_size > 0)
        {
            uint64_t currentTransactionIndex{0};
            bool isCoinbase{false};

            for (const Json::Value &tx : this->transactions)
            {
                if (tx.isNull())
                {
                    throw std::runtime_error("Invalid transaction at block height " + std::to_string(this->height) + ".");
                }

                std::string tx_id = tx["txid"].asString();

                // Transactions array -> Database list representation
                std::string transaction_ids_database_representation = "{";
                transaction_ids_database_representation += "\"" + tx_id + "\"";

                if (currentTransactionIndex < transactions_size - 1)
                {
                    transaction_ids_database_representation += ",";
                }

                transaction_ids_database_representation += "}";

                // Transaction inputs / outputs
                this->total_outputs += static_cast<uint64_t>(tx["vout"].size());
                this->total_inputs += static_cast<uint64_t>(tx["vin"].size());
                this->total_transparent_output += tx["vout"].asDouble();

                if (tx["vin"].isArray() && tx["vin"].size() == 1 && tx["vin"][0].isMember("coinbase"))
                {
                    this->total_transparent_input = 0.0;
                    isCoinbase = true;
                }
                else
                {
                    const std::optional<const pqxx::result> database_read_result = Database::ExecuteRead("SELECT * FROM transparent_outputs WHERE tx_id = $1 AND output_index = $2", tx["vin"]["txid"].asString(), static_cast<uint64_t>(tx["vin"]["vout"].asInt()));
                    if (database_read_result.has_value() && !database_read_result.value().empty())
                    {
                        this->total_transparent_input += database_read_result.value()[0]["value"].as<double>();
                    }
                }

                double current_total_block_public_input{0.0};
                double current_total_block_public_output{0.0};
                this->_storeTransparentInputs(tx_id, tx["vin"], conn, "insert_transparent_inputs", blockTransaction, current_total_block_public_input);
                this->_storeTransparentOutputs(tx_id, tx["vout"], conn, "insert_transparent_outputs", blockTransaction, current_total_block_public_output);

                blockTransaction.exec_prepared("insert_transaction", tx_id, std::to_string(tx.size()), tx["overwintered"].asCString(), tx["version"].asCString(), std::to_string(current_total_block_public_input), std::to_string(current_total_block_public_output), tx["hex"].asCString(), this->hash, this->timestamp, this->height, tx["vin"].size(), static_cast<uint64_t>(tx["vout"].size()));

                this->total_transparent_input += current_total_block_public_input;
                this->total_transparent_output += current_total_block_public_output;

                ++currentTransactionIndex;
            }

            blockTransaction.exec_prepared("insert_block", this->hash, this->height, this->timestamp, this->nonce, this->size, this->num_transactions, this->total_transparent_input, this->difficulty, this->chainwork, this->merkle_root, this->version, this->bits, this->transaction_ids_database_representation, this->total_outputs, this->total_inputs, this->total_transparent_output, "");
        }
    }
    catch (const std::exception &e)
    {
        __ERROR__(e.what());
        throw;
    }
}

void StoreableBlockData::_storeTransparentInputs(const std::string &tx_id, const Json::Value &inputs, std::unique_ptr<pqxx::connection> &conn, const std::string &prepared_statement, pqxx::work &blockTransaction, double &total_transparent_input)
{
    conn->prepare("insert_transparent_inputs",
                  R"(
                 INSERT INTO transparent_inputs 
                 (tx_id, vin_tx_id, v_out_idx, value, senders, coinbase)
                 VALUES ($1, $2, $3, $4, $5, $6)
                 ON CONFLICT (tx_id) 
                 DO NOTHING
              )");

    if (inputs.size() > 0)
    {

        std::string vin_tx_id;
        uint32_t v_out_idx;
        std::string coinbase{""};
        std::string senders{"{}"};

        double current_input_value{0.0};

        for (const Json::Value &input : inputs)
        {
            try
            {
                if (input.isMember("coinbase"))
                {
                    coinbase = input["coinbase"].asString();
                    vin_tx_id = "-1";
                    v_out_idx = 0; // Represent v_out_idx for coinbase transactions with alternative value.
                    senders = "{}";
                }
                else
                {
                    coinbase = "";
                    vin_tx_id = input["txid"].asString();
                    v_out_idx = input["vout"].asInt();

                    // Find the vout referenced in this vin to get the value and add to the total public input
                    const std::optional<const pqxx::result> database_read_result = Database::ExecuteRead("SELECT * FROM transparent_outputs WHERE tx_id = $1 AND output_index = $2", input["txid"].asString(), static_cast<uint64_t>(input["vout"].asInt()));
                    if (database_read_result.has_value())
                    {
                        pqxx::result db_read_result = database_read_result.value();
                        if (!db_read_result.empty())
                        {
                            pqxx::row output_specified_in_vin = db_read_result[0];
                            current_input_value = output_specified_in_vin["value"].as<double>();
                            senders = output_specified_in_vin["recipients"].as<std::string>();
                        }
                    }
                    else
                    {
                        senders = "{}";
                        current_input_value = 0.0;
                    }

                    total_transparent_input += current_input_value;
                }

                blockTransaction.exec_prepared(prepared_statement, tx_id, vin_tx_id, v_out_idx, current_input_value, senders, coinbase);

                senders = "{}";
                current_input_value = 0.0;
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
    }
}

void StoreableBlockData::_storeTransparentOutputs(const std::string &tx_id, const Json::Value &outputs, std::unique_ptr<pqxx::connection> &conn, const std::string &prepared_statement, pqxx::work &blockTransaction, double &total_public_output)
{
    conn->prepare("insert_transparent_outputs",
                  R"(
                 INSERT INTO transparent_outputs 
                 (tx_id, output_index, recipients, value)
                 VALUES ($1, $2, $3, $4)
              )");

    double currentOutputValue{0.0};

    // Transaction outputs
    if (outputs.size() > 0)
    {
        size_t outputIndex{0};
        std::vector<std::string> recipients;
        for (const Json::Value &vOutEntry : outputs)
        {
            try
            {
                outputIndex = vOutEntry["n"].asLargestInt();
                currentOutputValue = vOutEntry["value"].asDouble();
                total_public_output += currentOutputValue;

                // Stringify recipient list for addresses in vout
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
                blockTransaction.exec_prepared(prepared_statement, tx_id, outputIndex, recipientList, currentOutputValue);
                recipients.clear();
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
    }
}

StoreableBlockData &StoreableBlockData::GetStoreableBlockData()
{
    return *this;
}

// Block
Block::Block() {}

Block::Block(const Json::Value &rawBlock) : block(rawBlock), storeableData(rawBlock)
{
    if (rawBlock.isNull() || !this->block["tx"].isArray())
    {
        throw std::invalid_argument("Invalid JSON value for Block(rawBlock)");
    }
}

const Json::Value &Block::GetRawJson() const
{
    return this->block;
}

StoreableBlockData &Block::GetStoreableBlockData()
{
    return storeableData.GetStoreableBlockData();
}

const bool Block::isValid() const
{
    return !this->block.isNull();
}