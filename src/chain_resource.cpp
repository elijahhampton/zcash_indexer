#include "chain_resource.h"

Block::Block(const Json::Value &rawBlock) : block(rawBlock), transactionGroup(block["tx"])
{
    if (block.isNull() || !block["tx"].isArray())
    {
        throw std::invalid_argument("Invalid JSON value for Block(rawBlock)");
    }
}

const TransactionGroup &Block::GetTransactionGroup() const
{
    return transactionGroup;
}
void Block::Store(const Database &database) const
{
    database.StoreBlock(this);
    transactionGroup.Store(database);
}

TransactionGroup::TransactionGroup(const Json::Value &transactions)
{
    if (!transactions.isArray())
    {
        throw std::invalid_argument("Invalid JSON value for Block(rawBlock)");
    }
}
TransactionGroup::TransactionGroup(const Block &block) : transactions(block["tx"])
{
    if (block.isNull() || !block["tx"].isArray())
    {
        throw std::invalid_argument("Invalid JSON value for Block(rawBlock)");
    }
}

void TransactionGroup::Store(const Database &database) const
{
    database.StoreTransactions(database);
}