#include "identifier.h"

std::map<TableColumn, std::string> tableColumnsToString = {
    {TableColumn::Blocks, "blocks"},
    {TableColumn::Transactions, "transactions"},
    {TableColumn::Checkpoints, "checkpoints"},
    {TableColumn::TransparentInputs, "transparent_inputs"},
    {TableColumn::TransparentOutputs, "transparent_outputs"},
    {TableColumn::PeerInfo, "peerinfo"},
    {TableColumn::ChainInfo, "chain_info"}
};