#pragma once

#include "types.h"
#include "error_logger.h"
#include "database_connection.h"
#include <json/json.h>
#include <string>
#include <memory>
#include <functional>

class DatabaseManager {
private:
    std::unique_ptr<DatabaseConnection> db_conn;
    DatabaseConfig db_config;
    MultiDatabaseConfig multi_config;
    bool is_multi_config;
    bool is_interactive_mode;
    DataSource data_source;
    ErrorLogger* error_logger_ptr;
    std::function<void(const std::string&)> tui_output_func;

public:
    DatabaseManager();
    ~DatabaseManager();
    
    // Configuration loading methods
    bool loadDatabaseConfig(const std::string& config_file);
    bool loadMultiDatabaseConfig(const std::string& config_file);
    bool switchDatabase(const std::string& database_name);
    bool initializeDatabase();
    bool ensureSchemaExists();
    
    // Table creation methods (generic)
    bool createNDCTable();
    bool createDrugsFDATable();
    bool createDrugLabelTable();
    bool createFDANDCDataTable();
    
    // Database-specific schema creation helpers
    std::string getNDCTableSchema();
    std::string getDrugsFDATableSchema();
    std::string getDrugLabelTableSchema();
    std::string getFDANDCDataTableSchema();
    
    // Data insertion methods
    bool insertNDCRecord(const Json::Value& record);
    bool insertDrugsFDARecord(const Json::Value& record);
    bool insertDrugLabelRecord(const Json::Value& record);
    bool insertFDANDCRecord(const Json::Value& record);
    
    // Database utilities
    bool tableExists(const std::string& tableName);
    bool clearTable(const std::string& tableName);
    bool clearDatabase();
    std::string escapeString(const std::string& input);
    
    // Error handling
    bool isDuplicateKeyError(const std::string& error_msg);
    
    // Helper methods
    std::string getArrayAsString(const Json::Value& array);
    std::string formatDate(const std::string& dateStr);
    std::string formatNDCToStandard(const std::string& ndc);
    std::string getFirstElement(const Json::Value& parent, const std::string& key);
    void outputMessage(const std::string& message);
    
    // Setters
    void setDataSource(DataSource source) { data_source = source; }
    void setErrorLogger(ErrorLogger* logger) { error_logger_ptr = logger; }
    void setInteractiveMode(bool interactive) { is_interactive_mode = interactive; }
    void setTUIOutput(std::function<void(const std::string&)> output_func) { tui_output_func = output_func; }
    
    // Getters
    DatabaseConnection* getConnection() const { return db_conn.get(); }
    DatabaseType getDatabaseType() const { return db_config.type; }
    const DatabaseConfig& getDatabaseConfig() const { return db_config; }
};