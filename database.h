#pragma once

#include "types.h"
#include "error_logger.h"
#include <mysql/mysql.h>
#include <json/json.h>
#include <string>

class DatabaseManager {
private:
    MYSQL* mysql_conn;
    DatabaseConfig db_config;
    DataSource data_source;
    ErrorLogger* error_logger_ptr;

public:
    DatabaseManager();
    ~DatabaseManager();
    
    bool loadDatabaseConfig(const std::string& config_file);
    bool initializeDatabase();
    bool ensureSchemaExists();
    
    // Table creation methods
    bool createNDCTable();
    bool createDrugsFDATable();
    bool createDrugLabelTable();
    bool createFDANDCDataTable();
    
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
    
    // Setters
    void setDataSource(DataSource source) { data_source = source; }
    void setErrorLogger(ErrorLogger* logger) { error_logger_ptr = logger; }
    
    // Getters
    MYSQL* getConnection() const { return mysql_conn; }
};