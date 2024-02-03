// Config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <cstdlib>
#include <string>
#include <stdexcept>

class Config {
public:
    static std::string getEnv(const char* key, const std::string& defaultValue = "") {
        char* val = std::getenv(key);
        if (val == nullptr) {  
            if (defaultValue.empty()) {
                throw std::runtime_error(std::string("Environment variable ") + key + " is not set.");
            }
            return defaultValue;
        }
        return std::string(val);
    }

    static std::string getDatabaseHost() {
        return getEnv("DB_HOST", "localhost");
    }

    static std::string getDatabasePort() {
        return getEnv("DB_PORT", "5432");  // Default PostgreSQL port
    }

    static std::string getDatabaseName() {
        return getEnv("DB_NAME", "postgres");
    }

    static std::string getDatabaseUser() {
        return getEnv("DB_USER", "postgres");
    }

    static std::string getDatabasePassword() {
        return getEnv("DB_PASSWORD", "mysecretpassword");
    }

    static std::string getRpcUrl() {
        return getEnv("RPC_URL", "8232");
    }

    static std::string getRpcUsername() {
        return getEnv("RPC_USERNAME", "user");
    }

    static std::string getRpcPassword() {
        return getEnv("RPC_PASSWORD", "password");
    }

    static std::string getBlockChunkProcessingSize() {
        return getEnv("BLOCK_CHUNK_PROCESSING_SIZE", "500");
    }

    static std::string getAllowMultipleThreads() {
        return getEnv("ALLOW_MULTIPLE_THREADS", "false");
    }

};

#endif // CONFIG_H
