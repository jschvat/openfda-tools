#include "error_logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <ctime>

ErrorLogger::ErrorLogger(const std::string& output_dir, size_t max_entries) 
    : output_directory(output_dir), logging_enabled(true), max_entries_per_file(max_entries) {
    ensureOutputDirectory();
}

ErrorLogger::~ErrorLogger() {
    if (logging_enabled && (!errors.empty() || !duplicates.empty())) {
        std::cout << "📋 Writing error logs to files..." << std::endl;
        writeAllToSeparateFiles();
    }
}

void ErrorLogger::ensureOutputDirectory() {
    try {
        std::filesystem::create_directories(output_directory);
    } catch (const std::exception& e) {
        std::cerr << "⚠️  Warning: Could not create log directory " << output_directory 
                  << ": " << e.what() << std::endl;
        output_directory = "./";
    }
}

std::string ErrorLogger::generateFilename(const std::string& prefix) const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << output_directory << prefix << "_" 
       << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".json";
    
    return ss.str();
}

std::string ErrorLogger::errorTypeToString(ErrorType type) const {
    switch (type) {
        case ErrorType::DUPLICATE_RECORD: return "DUPLICATE_RECORD";
        case ErrorType::INVALID_JSON: return "INVALID_JSON";
        case ErrorType::MISSING_FIELD: return "MISSING_FIELD";
        case ErrorType::DATABASE_ERROR: return "DATABASE_ERROR";
        case ErrorType::NETWORK_ERROR: return "NETWORK_ERROR";
        case ErrorType::FILE_ERROR: return "FILE_ERROR";
        case ErrorType::PROCESSING_ERROR: return "PROCESSING_ERROR";
        default: return "UNKNOWN_ERROR";
    }
}

std::string ErrorLogger::timestampToString(const std::chrono::system_clock::time_point& tp) const {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void ErrorLogger::logError(ErrorType type, const std::string& message, 
                          const std::string& source_file, int record_index,
                          const Json::Value& record_data, const std::string& additional_info) {
    if (!logging_enabled) return;
    
    ErrorEntry entry;
    entry.type = type;
    entry.message = message;
    entry.source_file = source_file;
    entry.record_index = record_index;
    entry.record_data = record_data;
    entry.timestamp = std::chrono::system_clock::now();
    entry.additional_info = additional_info;
    
    errors.push_back(entry);
    
    // Auto-write if we hit the max entries limit
    if (errors.size() >= max_entries_per_file) {
        std::cout << "📝 Writing errors to file (reached max entries: " << max_entries_per_file << ")" << std::endl;
        writeErrorsToFile();
        clearErrors();
    }
}

void ErrorLogger::logDuplicate(const std::string& primary_key, const std::string& table_name,
                              const Json::Value& record_data, const std::string& source_file,
                              int record_index) {
    if (!logging_enabled) return;
    
    DuplicateEntry entry;
    entry.primary_key = primary_key;
    entry.table_name = table_name;
    entry.record_data = record_data;
    entry.timestamp = std::chrono::system_clock::now();
    entry.source_file = source_file;
    entry.record_index = record_index;
    
    duplicates.push_back(entry);
    
    // Auto-write if we hit the max entries limit
    if (duplicates.size() >= max_entries_per_file) {
        std::cout << "📝 Writing duplicates to file (reached max entries: " << max_entries_per_file << ")" << std::endl;
        writeDuplicatesToFile();
        clearDuplicates();
    }
}

void ErrorLogger::logDatabaseError(const std::string& error_msg, const Json::Value& record) {
    logError(ErrorType::DATABASE_ERROR, error_msg, "", -1, record);
}

void ErrorLogger::logJSONError(const std::string& error_msg, const std::string& source_file) {
    logError(ErrorType::INVALID_JSON, error_msg, source_file);
}

void ErrorLogger::logNetworkError(const std::string& error_msg, const std::string& url) {
    logError(ErrorType::NETWORK_ERROR, error_msg, "", -1, Json::Value::null, url);
}

void ErrorLogger::logProcessingError(const std::string& error_msg, int record_index, const Json::Value& record) {
    logError(ErrorType::PROCESSING_ERROR, error_msg, "", record_index, record);
}

bool ErrorLogger::writeErrorsToFile(const std::string& filename) {
    if (errors.empty()) return true;
    
    std::string output_file = filename.empty() ? generateFilename("errors") : filename;
    
    Json::Value root;
    root["metadata"]["total_errors"] = static_cast<int>(errors.size());
    root["metadata"]["generated_at"] = timestampToString(std::chrono::system_clock::now());
    root["metadata"]["version"] = "1.0";
    
    // Group errors by type for better organization
    Json::Value error_groups;
    for (const auto& error : errors) {
        std::string type_str = errorTypeToString(error.type);
        
        Json::Value error_obj;
        error_obj["message"] = error.message;
        error_obj["timestamp"] = timestampToString(error.timestamp);
        
        if (!error.source_file.empty()) {
            error_obj["source_file"] = error.source_file;
        }
        
        if (error.record_index >= 0) {
            error_obj["record_index"] = error.record_index;
        }
        
        if (!error.record_data.isNull()) {
            error_obj["record_data"] = error.record_data;
        }
        
        if (!error.additional_info.empty()) {
            error_obj["additional_info"] = error.additional_info;
        }
        
        error_groups[type_str].append(error_obj);
    }
    
    root["errors"] = error_groups;
    
    std::ofstream file(output_file);
    if (!file.is_open()) {
        std::cerr << "❌ Error: Could not create errors file: " << output_file << std::endl;
        return false;
    }
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    file.close();
    
    std::cout << "✅ Errors written to: " << output_file << " (" << errors.size() << " entries)" << std::endl;
    return true;
}

bool ErrorLogger::writeDuplicatesToFile(const std::string& filename) {
    if (duplicates.empty()) return true;
    
    std::string output_file = filename.empty() ? generateFilename("duplicates") : filename;
    
    Json::Value root;
    root["metadata"]["total_duplicates"] = static_cast<int>(duplicates.size());
    root["metadata"]["generated_at"] = timestampToString(std::chrono::system_clock::now());
    root["metadata"]["version"] = "1.0";
    
    // Group duplicates by table for better organization
    Json::Value duplicate_groups;
    for (const auto& duplicate : duplicates) {
        Json::Value dup_obj;
        dup_obj["primary_key"] = duplicate.primary_key;
        dup_obj["timestamp"] = timestampToString(duplicate.timestamp);
        dup_obj["record_data"] = duplicate.record_data;
        
        if (!duplicate.source_file.empty()) {
            dup_obj["source_file"] = duplicate.source_file;
        }
        
        if (duplicate.record_index >= 0) {
            dup_obj["record_index"] = duplicate.record_index;
        }
        
        duplicate_groups[duplicate.table_name].append(dup_obj);
    }
    
    root["duplicates"] = duplicate_groups;
    
    std::ofstream file(output_file);
    if (!file.is_open()) {
        std::cerr << "❌ Error: Could not create duplicates file: " << output_file << std::endl;
        return false;
    }
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    file.close();
    
    std::cout << "✅ Duplicates written to: " << output_file << " (" << duplicates.size() << " entries)" << std::endl;
    return true;
}

bool ErrorLogger::writeAllToSeparateFiles() {
    bool errors_ok = true;
    bool duplicates_ok = true;
    
    if (!errors.empty()) {
        errors_ok = writeErrorsToFile();
    }
    
    if (!duplicates.empty()) {
        duplicates_ok = writeDuplicatesToFile();
    }
    
    return errors_ok && duplicates_ok;
}

size_t ErrorLogger::getErrorCountByType(ErrorType type) const {
    size_t count = 0;
    for (const auto& error : errors) {
        if (error.type == type) count++;
    }
    return count;
}

Json::Value ErrorLogger::generateSummary() const {
    Json::Value summary;
    
    summary["total_errors"] = static_cast<int>(errors.size());
    summary["total_duplicates"] = static_cast<int>(duplicates.size());
    
    // Error breakdown by type
    Json::Value error_breakdown;
    error_breakdown["duplicate_records"] = static_cast<int>(getErrorCountByType(ErrorType::DUPLICATE_RECORD));
    error_breakdown["invalid_json"] = static_cast<int>(getErrorCountByType(ErrorType::INVALID_JSON));
    error_breakdown["missing_fields"] = static_cast<int>(getErrorCountByType(ErrorType::MISSING_FIELD));
    error_breakdown["database_errors"] = static_cast<int>(getErrorCountByType(ErrorType::DATABASE_ERROR));
    error_breakdown["network_errors"] = static_cast<int>(getErrorCountByType(ErrorType::NETWORK_ERROR));
    error_breakdown["file_errors"] = static_cast<int>(getErrorCountByType(ErrorType::FILE_ERROR));
    error_breakdown["processing_errors"] = static_cast<int>(getErrorCountByType(ErrorType::PROCESSING_ERROR));
    
    summary["error_breakdown"] = error_breakdown;
    
    // Duplicate breakdown by table
    Json::Value duplicate_breakdown;
    std::map<std::string, int> table_counts;
    for (const auto& duplicate : duplicates) {
        table_counts[duplicate.table_name]++;
    }
    
    for (const auto& [table, count] : table_counts) {
        duplicate_breakdown[table] = count;
    }
    
    summary["duplicate_breakdown"] = duplicate_breakdown;
    summary["generated_at"] = timestampToString(std::chrono::system_clock::now());
    
    return summary;
}

void ErrorLogger::printSummary() const {
    std::cout << "\n📊 Error and Duplicate Summary:" << std::endl;
    std::cout << "═══════════════════════════════════" << std::endl;
    
    std::cout << "Total Errors: " << errors.size() << std::endl;
    std::cout << "Total Duplicates: " << duplicates.size() << std::endl;
    
    if (!errors.empty()) {
        std::cout << "\n🚨 Error Breakdown:" << std::endl;
        std::cout << "  • Database Errors: " << getErrorCountByType(ErrorType::DATABASE_ERROR) << std::endl;
        std::cout << "  • JSON Errors: " << getErrorCountByType(ErrorType::INVALID_JSON) << std::endl;
        std::cout << "  • Network Errors: " << getErrorCountByType(ErrorType::NETWORK_ERROR) << std::endl;
        std::cout << "  • Processing Errors: " << getErrorCountByType(ErrorType::PROCESSING_ERROR) << std::endl;
        std::cout << "  • Missing Field Errors: " << getErrorCountByType(ErrorType::MISSING_FIELD) << std::endl;
        std::cout << "  • File Errors: " << getErrorCountByType(ErrorType::FILE_ERROR) << std::endl;
    }
    
    if (!duplicates.empty()) {
        std::cout << "\n🔄 Duplicate Breakdown by Table:" << std::endl;
        std::map<std::string, int> table_counts;
        for (const auto& duplicate : duplicates) {
            table_counts[duplicate.table_name]++;
        }
        
        for (const auto& [table, count] : table_counts) {
            std::cout << "  • " << table << ": " << count << " duplicates" << std::endl;
        }
    }
    
    std::cout << "═══════════════════════════════════" << std::endl;
}