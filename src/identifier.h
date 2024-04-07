#include <cstdint>
#include <string>
#include <map>

#ifndef IDENTIFIER_CPP
#define IDENTIFIER_CPP
    
enum TableColumn: uint32_t {
    Blocks,
    Transactions,
    Checkpoints,
    TransparentInputs,
    TransparentOutputs,
    PeerInfo,
    ChainInfo
};

extern std::map<TableColumn, std::string> tableColumnsToString;

#endif // IDENTIFIER_CPP