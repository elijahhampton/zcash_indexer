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
        if (val == nullptr) {  // Use default value if not found
            if (defaultValue.empty()) {
                throw std::runtime_error(std::string("Environment variable ") + key + " is not set.");
            }
            return defaultValue;
        }
        return std::string(val);
    }

    static std::string getDatabaseHost() {
        return getEnv("DB_HOST");
    }

    static std::string getDatabasePort() {
        return getEnv("DB_PORT", "5432");  // Default PostgreSQL port
    }

    static std::string getDatabaseName() {
        return getEnv("DB_NAME");
    }

    static std::string getDatabaseUser() {
        return getEnv("DB_USER");
    }

    static std::string getDatabasePassword() {
        return getEnv("DB_PASSWORD");
    }

    static std::string getRpcUrl() {
        return getEnv("RPC_URL");
    }

    static std::string getRpcUsername() {
        return getEnv("RPC_USERNAME");
    }

    static std::string getRpcPassword() {
        return getEnv("RPC_PASSWORD");
    }

    // Add other getters for additional configuration as needed...
};

#endif // CONFIG_H
