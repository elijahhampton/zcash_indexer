#include "chain_resource.h"
#include "./database/database.h"

// Block
Block::Block() {}

Block::Block(const Json::Value &rawBlock) : nonce(rawBlock["nonce"].asCString()),
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
    if (rawBlock.isNull() || !this->block["tx"].isArray())
    {
        throw std::invalid_argument("Invalid JSON value for Block(rawBlock)");
    }
}

Json::Value &Block::GetRawJson()
{
    return this->block;
}

bool Block::isValid()
{
    return !this->block.isNull();
}

std::map<std::string, std::vector<std::vector<BlockData>>> Block::DataToOrmStorageMap()
{
    std::map<std::string, std::vector<std::vector<BlockData>>> orm_storage_map = {
        {"block", {}}, {"transaction", {}}, {"transparent_input", {}}, {"transparent_output", {}}};

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

                this->_storeTransparentInputs(tx_id, tx["vin"], current_total_block_public_input, orm_storage_map["transparent_input"]);
                this->_storeTransparentOutputs(tx_id, tx["vout"], current_total_block_public_output, orm_storage_map["transparent_output"]);

                this->total_transparent_input += current_total_block_public_input;
                this->total_transparent_output += current_total_block_public_output;

                std::vector<BlockData> transaction_data = {
                    tx_id,
                    std::to_string(tx.size()),
                    tx["overwintered"].asString(),
                    tx["version"].asString(),
                    std::to_string(current_total_block_public_input),
                    std::to_string(current_total_block_public_output),
                    tx["hex"].asString(),
                    this->hash,
                    this->timestamp,
                    this->height,
                    std::to_string(tx["vin"].size()),
                    std::to_string(static_cast<uint64_t>(tx["vout"].size()))};

                orm_storage_map["transaction"].emplace_back(transaction_data);
                ++currentTransactionIndex;
            }
        }

        std::vector<BlockData> block_data = {
            this->hash,
            std::to_string(this->height),
            this->timestamp,
            this->nonce,
            std::to_string(this->size),
            std::to_string(this->num_transactions),
            std::to_string(this->total_transparent_output),
            std::to_string(this->difficulty),
            this->chainwork,
            this->merkle_root,
            std::to_string(this->version),
            this->bits,
            this->transaction_ids_database_representation,
            std::to_string(this->total_outputs),
            std::to_string(this->total_inputs),
            std::to_string(this->total_transparent_input),
            ""};

        orm_storage_map["block"].emplace_back(block_data);
    }
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
        throw;
    }

    return orm_storage_map;
}

void Block::_storeTransparentInputs(const std::string &tx_id, const Json::Value &inputs, double &total_transparent_input, std::vector<std::vector<BlockData>> &transparent_transaction_inputs_values)
{

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
                    v_out_idx = input["vout"].asUInt64();

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

                senders = "{}";
                current_input_value = 0.0;

                std::vector<BlockData> temp_vec;
                temp_vec.push_back(tx_id);
                temp_vec.push_back(vin_tx_id);
                temp_vec.push_back(static_cast<uint64_t>(v_out_idx));
                temp_vec.push_back(current_input_value);
                temp_vec.push_back(senders);
                temp_vec.push_back(coinbase);

                transparent_transaction_inputs_values.push_back(temp_vec);
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
}

void Block::_storeTransparentOutputs(const std::string &tx_id, const Json::Value &outputs, double &total_public_output, std::vector<std::vector<BlockData>> &transparent_transaction_output_values)
{

    double currentOutputValue{0.0};

    // Transaction outputs
    if (outputs.size() > 0)
    {
        size_t outputIndex{0};
        std::vector<std::string> recipients;
        std::string recipientList;
        for (const Json::Value &vOutEntry : outputs)
        {
            try
            {
                outputIndex = vOutEntry["n"].asLargestInt();
                currentOutputValue = vOutEntry["value"].asDouble();
                total_public_output += currentOutputValue;

                // Stringify recipient list for addresses in vout
                Json::Value vOutAddresses = vOutEntry["scriptPubKey"]["addresses"];
                recipientList = "{";
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

                transparent_transaction_output_values.emplace_back(std::vector<BlockData>{
                    tx_id,
                    std::to_string(outputIndex),
                    recipientList,
                    std::to_string(currentOutputValue)});

                recipients.clear();
                recipientList.clear();
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
}