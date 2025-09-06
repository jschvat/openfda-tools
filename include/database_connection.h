#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <memory>
#include <mysql/mysql.h>
#include <libpq-fe.h>

// Abstract base class for database connections
class DatabaseConnection {
public:
    virtual ~DatabaseConnection() = default;
    virtual bool connect(const DatabaseConfig& config) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual bool executeQuery(const std::string& query) = 0;
    virtual bool executeQuery(const std::string& query, std::vector<std::vector<std::string>>& results) = 0;
    virtual std::string escapeString(const std::string& str) = 0;
    virtual std::string getLastError() const = 0;
    virtual DatabaseType getType() const = 0;
};

// MySQL connection implementation
class MySQLConnection : public DatabaseConnection {
private:
    MYSQL* connection;
    std::string last_error;

public:
    MySQLConnection();
    virtual ~MySQLConnection();
    
    bool connect(const DatabaseConfig& config) override;
    void disconnect() override;
    bool isConnected() const override;
    bool executeQuery(const std::string& query) override;
    bool executeQuery(const std::string& query, std::vector<std::vector<std::string>>& results) override;
    std::string escapeString(const std::string& str) override;
    std::string getLastError() const override;
    DatabaseType getType() const override { return DatabaseType::MYSQL; }
};

// PostgreSQL connection implementation
class PostgreSQLConnection : public DatabaseConnection {
private:
    PGconn* connection;
    std::string last_error;

public:
    PostgreSQLConnection();
    virtual ~PostgreSQLConnection();
    
    bool connect(const DatabaseConfig& config) override;
    void disconnect() override;
    bool isConnected() const override;
    bool executeQuery(const std::string& query) override;
    bool executeQuery(const std::string& query, std::vector<std::vector<std::string>>& results) override;
    std::string escapeString(const std::string& str) override;
    std::string getLastError() const override;
    DatabaseType getType() const override { return DatabaseType::POSTGRESQL; }
};

// Factory function to create appropriate database connection
std::unique_ptr<DatabaseConnection> createDatabaseConnection(DatabaseType type);