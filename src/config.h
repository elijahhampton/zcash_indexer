// Config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <cstdlib>
#include <string>
#include <stdexcept>

class Config {
public:
    static std::string getEnvAsString(const char* key, const std::string& defaultValue = "") {
        char* val = std::getenv(key);
        if (val == nullptr) {
            if (defaultValue.empty()) {
                throw std::runtime_error(std::string("Environment variable ") + key + " is not set.");
            }
            return defaultValue;
        }
        return std::string(val);
    }

    static int getEnvAsInt(const char* key, int defaultValue) {
        std::string valStr = getEnvAsString(key, std::to_string(defaultValue));
        return std::stoi(valStr);
    }

    static bool getEnvAsBool(const char* key, bool defaultValue) {
        std::string valStr = getEnvAsString(key, defaultValue ? "true" : "false");
        std::transform(valStr.begin(), valStr.end(), valStr.begin(), ::tolower);
        return valStr == "true" || valStr == "1";
    }

    static std::string getDatabaseHost() {
        return getEnvAsString("DB_HOST", "localhost");
    }

    static std::string getDatabasePort() {
        return getEnvAsString("DB_PORT", "5432");  // Default PostgreSQL port
    }

    static std::string getDatabaseName() {
        return getEnvAsString("DB_NAME", "postgres");
    }

    static std::string getDatabaseUser() {
        return getEnvAsString("DB_USER", "postgres");
    }

    static std::string getDatabasePassword() {
        return getEnvAsString("DB_PASSWORD", "mysecretpassword");
    }

    static std::string getRpcUrl() {
        return getEnvAsString("RPC_URL", "127.0.0.1:18232");
    }

    static std::string getRpcUsername() {
        return getEnvAsString("RPC_USERNAME", "user");
    }

    static std::string getRpcPassword() {
        return getEnvAsString("RPC_PASSWORD", "password");
    }

    static int getBlockChunkProcessingSize() {
        return getEnvAsInt("BLOCK_CHUNK_PROCESSING_SIZE", 500);
    }

    static int8_t getAllowMultipleThreads() {
        return getEnvAsInt("ALLOW_MULTIPLE_THREADS", 1);
    }

    static int8_t getMonitorPeers() {
        return getEnvAsInt("MONITOR_PEERS", 1);
    }

    static int8_t getMonitorChainInfo() {
        return getEnvAsInt("MONITOR_CHAIN_INFO", 1);
    }

};

#endif // CONFIG_H
