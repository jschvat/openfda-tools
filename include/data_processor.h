#pragma once

#include "types.h"
#include "database.h"
#include "error_logger.h"
#include <json/json.h>
#include <string>

class DataProcessor {
private:
    DatabaseManager& db_manager;
    ErrorLogger error_logger;
    DataSource data_source;
    std::string temp_dir;
    std::string current_source_file;

public:
    DataProcessor(DatabaseManager& db_manager, const std::string& log_dir = "./logs/");
    ~DataProcessor() = default;
    
    void setDataSource(DataSource source) { data_source = source; }
    
    // Error logging controls
    void setErrorLoggingEnabled(bool enabled) { error_logger.setLoggingEnabled(enabled); }
    bool isErrorLoggingEnabled() const { return error_logger.isLoggingEnabled(); }
    void printErrorSummary() const { error_logger.printSummary(); }
    
    // JSON processing methods
    bool parseAndInsertData(const std::string& json_data);
    bool processBulkJsonFile(const std::string& json_filename);
    
    // File operations
    bool extractZipFile(const std::string& zip_filename);
    
    // Bulk processing workflow
    bool processBulkDownload(const DataSourceConfig& config);
    
    // Main processing entry point
    void processAllData();
    
    // API data processing (for NDC_API)
    std::string downloadNDCData(int limit = 1000, int skip = 0);
    
private:
    // Helper methods
    std::string buildUrl(const DataSourceConfig& config, int part);
    std::string extractPrimaryKey(const Json::Value& record);
    std::string getTableNameForDataSource(DataSource source);
};