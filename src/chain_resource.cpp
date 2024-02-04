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

        this->ProcessBlockToStoreable(rawBlock);
    }

    this->isValid = false;
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
    this->ProcessBlockToStoreable(rawBlock);
}


void StoreableBlockData::ProcessBlockToStoreable(const Block &rawBlock)
{
    std::string transaction_ids_sql_representation = "{";

    const uint64_t transactions_size = this->transactions.size();

    if (this->transactions.isArray() && transactions_size > 0)
    {
        for (uint64_t i = 0; i < transactions_size; ++i)
        {
            this->total_outputs += static_cast<uint64_t>(this->transactions[static_cast<Json::Value::ArrayIndex>(i)]["vout"].size());
            this->total_inputs += static_cast<uint64_t>(this->transactions[static_cast<Json::Value::ArrayIndex>(i)]["vin"].size());

            if (this->transactions[static_cast<Json::Value::ArrayIndex>(i)].isMember("txid") && this->transactions[static_cast<Json::Value::ArrayIndex>(i)]["txid"].isString())
            {
                transaction_ids_sql_representation += "\"" + this->transactions[static_cast<Json::Value::ArrayIndex>(i)]["txid"].asString() + "\"";
                if (i < transactions_size - 1)
                {
                    transaction_ids_sql_representation += ",";
                }
            }
        }
    }

    transaction_ids_sql_representation += "}";
    for (const Json::Value &tx : this->transactions)
    {
        for (const Json::Value &voutItem : tx["vout"])
        {
            this->total_transparent_output += voutItem["value"].asDouble();
        }

        if (tx["vin"].size() == 1)
        {
            this->total_transparent_input = 0;
        }
        else
        {
            Json::Value output_specified_in_vin;
            for (const Json::Value &vinItem : tx["vin"])
            {
                const std::optional<const pqxx::result> database_read_result = Database::ExecuteRead("SELECT * FROM transparent_outputs WHERE tx_id = $1 AND output_index = $2", vinItem["txid"].asString(), static_cast<uint64_t>(vinItem["vout"].asInt()));
                if (database_read_result.has_value() && !database_read_result.value().empty()) {
                    this->total_transparent_input += database_read_result.value()[0]["value"].as<double>(); 
                }
                else
                {
                    this->total_transparent_input += 0.0;
                }

                output_specified_in_vin = 0;
            }
        }
    }
}

const StoreableBlockData &StoreableBlockData::GetStoreableBlockData() const
{
    return *this;
}

// Block
Block::Block() {}

Block::Block(const Json::Value &rawBlock) : block(rawBlock), transactionGroup(this->block["tx"]), storeableData(rawBlock)
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

const StoreableBlockData &Block::GetStoreableBlockData() const
{
    return storeableData.GetStoreableBlockData();
}

// Transaction Group

const TransactionGroup &Block::GetTransactionGroup() const
{
    return transactionGroup;
}

const bool Block::isValid() const
{
    return !this->block.isNull();
}

TransactionGroup::TransactionGroup() {}

TransactionGroup::TransactionGroup(const Json::Value &transactions)
{
    if (!transactions.isArray())
    {
        throw std::invalid_argument("Invalid JSON value for Block(rawBlock)");
    }
}
TransactionGroup::TransactionGroup(const Block &block) : transactions(block.GetTransactionGroup().GetRawTransactions())
{
}

const Json::Value &TransactionGroup::GetRawTransactions() const
{
    return this->transactions;
}
