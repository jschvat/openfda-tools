#pragma once

#include <json/json.h>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>

enum class ErrorType {
    DUPLICATE_RECORD,
    INVALID_JSON,
    MISSING_FIELD,
    DATABASE_ERROR,
    NETWORK_ERROR,
    FILE_ERROR,
    PROCESSING_ERROR
};

struct ErrorEntry {
    ErrorType type;
    std::string message;
    std::string source_file;
    int record_index;
    Json::Value record_data;
    std::chrono::system_clock::time_point timestamp;
    std::string additional_info;
};

struct DuplicateEntry {
    std::string primary_key;
    std::string table_name;
    Json::Value record_data;
    std::chrono::system_clock::time_point timestamp;
    std::string source_file;
    int record_index;
};

class ErrorLogger {
private:
    std::vector<ErrorEntry> errors;
    std::vector<DuplicateEntry> duplicates;
    std::string output_directory;
    bool logging_enabled;
    size_t max_entries_per_file;
    
public:
    ErrorLogger(const std::string& output_dir = "./logs/", size_t max_entries = 10000);
    ~ErrorLogger();
    
    // Enable/disable logging
    void setLoggingEnabled(bool enabled) { logging_enabled = enabled; }
    bool isLoggingEnabled() const { return logging_enabled; }
    
    // Error logging
    void logError(ErrorType type, const std::string& message, 
                  const std::string& source_file = "", int record_index = -1,
                  const Json::Value& record_data = Json::Value::null,
                  const std::string& additional_info = "");
    
    // Duplicate logging
    void logDuplicate(const std::string& primary_key, const std::string& table_name,
                      const Json::Value& record_data, const std::string& source_file = "",
                      int record_index = -1);
    
    // Convenience methods for common error types
    void logDatabaseError(const std::string& error_msg, const Json::Value& record = Json::Value::null);
    void logJSONError(const std::string& error_msg, const std::string& source_file = "");
    void logNetworkError(const std::string& error_msg, const std::string& url = "");
    void logProcessingError(const std::string& error_msg, int record_index = -1, 
                           const Json::Value& record = Json::Value::null);
    
    // File output
    bool writeErrorsToFile(const std::string& filename = "");
    bool writeDuplicatesToFile(const std::string& filename = "");
    bool writeAllToSeparateFiles();
    
    // Statistics
    size_t getErrorCount() const { return errors.size(); }
    size_t getDuplicateCount() const { return duplicates.size(); }
    size_t getErrorCountByType(ErrorType type) const;
    
    // Summary
    Json::Value generateSummary() const;
    void printSummary() const;
    
    // Clear logs
    void clearErrors() { errors.clear(); }
    void clearDuplicates() { duplicates.clear(); }
    void clearAll() { clearErrors(); clearDuplicates(); }
    
private:
    std::string errorTypeToString(ErrorType type) const;
    std::string timestampToString(const std::chrono::system_clock::time_point& tp) const;
    void ensureOutputDirectory();
    std::string generateFilename(const std::string& prefix) const;
};