#include "chain_resource.h"
#include "database/database.h"

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
                                            num_transactions(rawBlock["tx"].size()),
                                            hash(rawBlock["hash"].asCString()),
                                            height(rawBlock["height"].asUInt64()),
                                            size(rawBlock["size"].asUInt64()),
                                            chainwork(rawBlock["chainwork"].asCString()),
                                            bits(rawBlock["bits"].asCString())
{
    if (rawBlock.isNull())
    {
        throw std::invalid_argument("Invalid JSON value for Block(rawBlock)");
    }
}

Json::Value &Block::GetRawJson()
{
    return block;
}

bool Block::isValid()
{
    return block.isNull();
}

std::map<std::string, std::vector<std::vector<BlockData>>> Block::DataToOrmStorageMap()
{
    std::map<std::string, std::vector<std::vector<BlockData>>> orm_storage_map = {
        {tableColumnsToString[TableColumn::Blocks], {}}, {tableColumnsToString[TableColumn::Transactions], {}}, {tableColumnsToString[TableColumn::TransparentInputs], {}}, {tableColumnsToString[TableColumn::TransparentOutputs], {}}};

    try
    {
        transaction_ids_database_representation ="{}";
        if (!this->transactions.isNull() && this->transactions.isArray() && num_transactions > 0)
        {
            size_t currentTransactionIndex{0};
            bool isCoinbase{false};

            // Transactions array -> Database list representation
            transaction_ids_database_representation = "{";
            for (const Json::Value &tx : this->transactions)
            {
                const std::string tx_id = tx["txid"].asString();
                transaction_ids_database_representation += "\"" + tx_id + "\"";
                if (currentTransactionIndex < num_transactions - 1)
                {
                    transaction_ids_database_representation += ",";
                }

                // Transaction inputs / outputs
                total_outputs += static_cast<size_t>(tx["vout"].size());
                total_inputs += static_cast<size_t>(tx["vin"].size());
                
                if (tx["vin"].isArray() && tx["vin"].size() == 1 && tx["vin"][0].isMember("coinbase"))
                {
                    total_transparent_input = 0.0;
                    isCoinbase = true;
                }
                else
                {
                    const std::optional<const pqxx::result> database_read_result = Database::ExecuteRead("SELECT * FROM transparent_outputs WHERE tx_id = $1 AND output_index = $2", tx["vin"]["txid"].asString(), static_cast<uint64_t>(tx["vin"]["vout"].asInt()));
                    if (database_read_result.has_value() && !database_read_result.value().empty())
                    {
                        total_transparent_input += database_read_result.value()[0]["value"].as<double>();
                    }
                }

                double current_total_block_public_input{0.0};
                double current_total_block_public_output{0.0};

                TransparentInputs(tx_id, tx["vin"], current_total_block_public_input, orm_storage_map[tableColumnsToString[TableColumn::TransparentInputs]]);
                TransparentOutputs(tx_id, tx["vout"], current_total_block_public_output, orm_storage_map[tableColumnsToString[TableColumn::TransparentOutputs]]);

                total_transparent_input += current_total_block_public_input;
                total_transparent_output += current_total_block_public_output;

                std::vector<BlockData> transaction_data = {
                    tx_id,
                    std::to_string(tx.size()),
                    tx["overwintered"].asString(),
                    tx["version"].asString(),
                    std::to_string(current_total_block_public_input),
                    std::to_string(current_total_block_public_output),
                    tx["hex"].asString(),
                    hash,
                    timestamp,
                    height,
                    std::to_string(tx["vin"].size()),
                    std::to_string(static_cast<size_t>(tx["vout"].size()))
                };

                orm_storage_map[tableColumnsToString[TableColumn::Transactions]].emplace_back(std::move(transaction_data));
                ++currentTransactionIndex;
            }
            transaction_ids_database_representation += "}";
        }

        std::vector<BlockData> block_data = {
            std::move(hash),
            std::to_string(height),
            std::move(timestamp),
            std::move(nonce),
            std::to_string(size),
            std::to_string(num_transactions),
            std::to_string(total_transparent_output),
            std::to_string(difficulty),
            std::move(chainwork),
            std::move(merkle_root),
            std::to_string(version),
            std::move(bits),
            std::move(transaction_ids_database_representation),
            std::to_string(total_outputs),
            std::to_string(total_inputs),
            std::to_string(total_transparent_input),
            ""};

        orm_storage_map[tableColumnsToString[TableColumn::Blocks]].emplace_back(block_data);
    }
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
        throw;
    }

    return orm_storage_map;
}

void Block::TransparentInputs(const std::string &tx_id, const Json::Value &inputs, double &total_transparent_input, std::vector<std::vector<BlockData>> &transparent_transaction_inputs_values)
{
    if (inputs.isArray() && inputs.size() > 0)
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

                std::vector<BlockData> temp_vec = {
                    tx_id,
                    std::move(vin_tx_id),
                    static_cast<uint64_t>(v_out_idx),
                    current_input_value,
                    std::move(senders),
                    std::move(coinbase)
                };

                transparent_transaction_inputs_values.emplace_back(temp_vec);
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

            // Reset for next iteration
            senders = "{}";
            current_input_value = 0.0;
        }
    }
}

void Block::TransparentOutputs(const std::string &tx_id, const Json::Value &outputs, double &total_public_output, std::vector<std::vector<BlockData>> &transparent_transaction_output_values)
{

    double currentOutputValue{0.0};

    // Transaction outputs
    if (outputs.size() > 0)
    {
        size_t outputIndex{0};
        std::string recipientList;
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
                recipientList = "{";
                if (vOutAddresses.isArray() && vOutAddresses.size() > 0)
                {
                    recipients.reserve(vOutAddresses.size());
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
                    std::move(recipientList),
                    std::to_string(currentOutputValue)
                });

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