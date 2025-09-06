#include "data_processor.h"
#include "network.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdio>

DataProcessor::DataProcessor(DatabaseManager& db_manager, const std::string& log_dir) 
    : db_manager(db_manager), error_logger(log_dir), temp_dir("./temp_openfda/") {
    // Create temp directory if it doesn't exist
    mkdir(temp_dir.c_str(), 0755);
    
    // Connect error logger to database manager
    db_manager.setErrorLogger(&error_logger);
}

std::string DataProcessor::buildUrl(const DataSourceConfig& config, int part) {
    std::string url = config.base_url + config.file_pattern;
    
    if (config.is_multi_file) {
        // Replace format specifiers for multi-file downloads
        size_t pos = url.find("{:04d}");
        if (pos != std::string::npos) {
            // First {:04d} = part number
            char buffer[10];
            snprintf(buffer, sizeof(buffer), "%04d", part);
            url.replace(pos, 6, buffer);
            
            // Second {:04d} = total file count
            pos = url.find("{:04d}", pos + 4);
            if (pos != std::string::npos) {
                snprintf(buffer, sizeof(buffer), "%04d", config.file_count);
                url.replace(pos, 6, buffer);
            }
        }
    }
    
    return url;
}

bool DataProcessor::extractZipFile(const std::string& zip_filename) {
    std::cout << "⏳ Extracting ZIP file..." << std::endl;
    
    // Use unzip command to extract the file
    // Create safe filename for shell command (replace spaces with underscores)
    std::string safe_filename = zip_filename;
    std::replace(safe_filename.begin(), safe_filename.end(), ' ', '_');
    
    std::string extract_command = "cd \"" + temp_dir + "\" && unzip -o \"" + safe_filename + "\"";
    
    int result = system(extract_command.c_str());
    if (result != 0) {
        std::cerr << "❌ Error: Failed to extract ZIP file " << zip_filename << std::endl;
        return false;
    }
    
    std::cout << "✓ ZIP file extracted successfully" << std::endl;
    return true;
}

bool DataProcessor::processBulkJsonFile(const std::string& json_filename) {
    std::cout << "📄 Processing JSON file: " << json_filename << std::endl;
    
    // Set current source file for error tracking
    current_source_file = json_filename;
    
    std::string full_path = temp_dir + json_filename;
    std::ifstream file(full_path);
    if (!file.is_open()) {
        std::cerr << "❌ Error: Cannot open JSON file " << full_path << std::endl;
        error_logger.logError(ErrorType::FILE_ERROR, 
            "Cannot open JSON file: " + full_path, current_source_file);
        return false;
    }
    
    // Read the entire file
    std::string json_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    if (json_data.empty()) {
        std::cerr << "❌ Error: JSON file is empty" << std::endl;
        error_logger.logError(ErrorType::FILE_ERROR, "JSON file is empty", current_source_file);
        return false;
    }
    
    // Parse JSON
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    Json::Value root;
    std::string parse_errors;
    
    if (!reader->parse(json_data.c_str(), json_data.c_str() + json_data.size(), &root, &parse_errors)) {
        std::cerr << "❌ Error parsing JSON: " << parse_errors << std::endl;
        error_logger.logJSONError("JSON parsing failed: " + parse_errors, current_source_file);
        delete reader;
        return false;
    }
    delete reader;
    
    if (!root.isMember("results") || !root["results"].isArray()) {
        std::cerr << "❌ Error: Invalid JSON structure - missing results array" << std::endl;
        error_logger.logJSONError("Invalid JSON structure - missing results array", current_source_file);
        return false;
    }
    
    const Json::Value& results = root["results"];
    int total_records = results.size();
    int processed_count = 0;
    int duplicate_count = 0;
    
    std::cout << "✓ Found " << total_records << " records to process" << std::endl;
    std::cout << "⏳ Inserting data into database..." << std::endl;
    
    for (int i = 0; i < total_records; i++) {
        const Json::Value& record = results[i];
        
        // Capture cout to detect duplicate messages
        std::streambuf* old_cout = std::cout.rdbuf();
        std::ostringstream capture_stream;
        std::cout.rdbuf(capture_stream.rdbuf());
        
        bool success = false;
        if (data_source == DataSource::DRUGSFDA_BULK) {
            success = db_manager.insertDrugsFDARecord(record);
        } else if (data_source == DataSource::DRUG_LABEL_BULK) {
            success = db_manager.insertDrugLabelRecord(record);
        } else if (data_source == DataSource::FDA_NDC_BULK) {
            success = db_manager.insertFDANDCRecord(record);
        } else {
            success = db_manager.insertNDCRecord(record);
        }
        
        // Restore cout
        std::cout.rdbuf(old_cout);
        std::string captured_output = capture_stream.str();
        
        if (success) {
            processed_count++;
            if (captured_output.find("Record already exists") != std::string::npos) {
                duplicate_count++;
                
                // Log the duplicate with full record data
                std::string primary_key = extractPrimaryKey(record);
                std::string table_name = getTableNameForDataSource(data_source);
                error_logger.logDuplicate(primary_key, table_name, record, current_source_file, i);
            }
        } else {
            // Log processing error
            error_logger.logProcessingError(
                "Failed to insert record into database", i, record);
        }
        
        // Print the captured duplicate message if any
        if (!captured_output.empty() && captured_output.find("Record already exists") != std::string::npos) {
            std::cout << captured_output;
        }
        
        if (i % 1000 == 0 && i > 0) {
            int percent = (i * 100) / total_records;
            std::cout << "📊 Progress: " << i << " / " << total_records << " (" 
                     << percent << "%) - " << (processed_count - duplicate_count) << " new, " 
                     << duplicate_count << " duplicates" << std::endl;
        }
    }
    
    std::cout << "✅ Successfully processed " << processed_count << " / " << total_records 
              << " records (" << (processed_count - duplicate_count) << " new, " 
              << duplicate_count << " duplicates)" << std::endl;
    return true;
}

bool DataProcessor::processBulkDownload(const DataSourceConfig& config) {
    std::cout << "🚀 Starting " << config.name << " bulk data processing..." << std::endl;
    
    NetworkManager network;
    
    if (config.is_multi_file) {
        // Handle multi-file downloads (like Drug Label with 13 parts)
        int successful_parts = 0;
        
        for (int part = 1; part <= config.file_count; part++) {
            std::cout << "\n📦 Processing " << config.name << " part " << part << " of " << config.file_count << std::endl;
            
            // Build URL and filename for this part
            std::string url = buildUrl(config, part);
            std::string filename = config.name + "_part_" + std::to_string(part) + ".zip";
            std::string safe_filename = filename;
            std::replace(safe_filename.begin(), safe_filename.end(), ' ', '_');
            
            // Download the file
            if (!network.downloadBulkFile(url, temp_dir + safe_filename)) {
                std::cerr << "❌ Failed to download " << config.name << " part " << part << std::endl;
                continue;
            }
            
            // Extract the ZIP file
            if (!extractZipFile(safe_filename)) {
                std::cerr << "❌ Failed to extract " << config.name << " part " << part << std::endl;
                continue;
            }
            
            // Find the JSON file in the extracted content
            std::string json_pattern = std::string("drug-") + (config.name == "Drug Label" ? "label" : "unknown");
            std::string expected_json = json_pattern + "-" + 
                                      std::string(4 - std::to_string(part).length(), '0') + std::to_string(part) + 
                                      "-of-" + 
                                      std::string(4 - std::to_string(config.file_count).length(), '0') + std::to_string(config.file_count) + 
                                      ".json";
            
            // Process the JSON file
            if (!processBulkJsonFile(expected_json)) {
                std::cerr << "❌ Failed to process " << config.name << " part " << part << std::endl;
                continue;
            }
            
            successful_parts++;
            std::cout << "✅ Successfully processed " << config.name << " part " << part << std::endl;
        }
        
        if (successful_parts > 0) {
            std::cout << "🎉 " << config.name << " bulk data processing completed! " 
                     << "Successfully processed " << successful_parts << " out of " 
                     << config.file_count << " parts." << std::endl;
            
            // Print error summary at the end of processing
            if (error_logger.getErrorCount() > 0 || error_logger.getDuplicateCount() > 0) {
                std::cout << "\n" << std::endl;
                error_logger.printSummary();
            }
            
            return true;
        } else {
            std::cerr << "❌ No " << config.name << " parts were successfully processed!" << std::endl;
            return false;
        }
    } else {
        // Handle single-file downloads
        std::string url = buildUrl(config, 1);
        std::string filename = config.name + "_bulk.zip";
        std::string safe_filename = filename;
        std::replace(safe_filename.begin(), safe_filename.end(), ' ', '_');
        
        if (!network.downloadBulkFile(url, temp_dir + safe_filename)) {
            std::cerr << "❌ Failed to download " << config.name << " bulk file" << std::endl;
            return false;
        }
        
        if (!extractZipFile(safe_filename)) {
            std::cerr << "❌ Failed to extract " << config.name << " bulk file" << std::endl;
            return false;
        }
        
        // Find the JSON file - typically matches the ZIP name but with .json extension
        std::string json_filename;
        if (config.name == "NDC" || config.name == "FDA NDC Data") {
            json_filename = "drug-ndc-0001-of-0001.json";
        } else if (config.name == "DrugsFDA") {
            json_filename = "drug-drugsfda-0001-of-0001.json";
        } else {
            // Generic fallback
            json_filename = safe_filename.substr(0, safe_filename.length() - 4) + ".json";
        }
        
        if (!processBulkJsonFile(json_filename)) {
            std::cerr << "❌ Failed to process JSON file" << std::endl;
            return false;
        }
        
        std::cout << "🎉 " << config.name << " bulk data processing completed successfully!" << std::endl;
        
        // Print error summary at the end of processing
        if (error_logger.getErrorCount() > 0 || error_logger.getDuplicateCount() > 0) {
            std::cout << "\n" << std::endl;
            error_logger.printSummary();
        }
        
        return true;
    }
}

std::string DataProcessor::downloadNDCData(int limit, int skip) {
    NetworkManager network;
    
    std::string url = "https://api.fda.gov/drug/ndc.json?limit=" + std::to_string(limit) + "&skip=" + std::to_string(skip);
    std::cout << "⏳ Downloading from: " << url << std::endl;
    
    return network.downloadData(url);
}

bool DataProcessor::parseAndInsertData(const std::string& json_data) {
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    Json::Value root;
    std::string errors;
    
    if (!reader->parse(json_data.c_str(), json_data.c_str() + json_data.size(), &root, &errors)) {
        std::cerr << "Error parsing JSON: " << errors << std::endl;
        delete reader;
        return false;
    }
    delete reader;
    
    if (!root.isMember("results") || !root["results"].isArray()) {
        std::cerr << "Error: Invalid JSON structure - missing results array" << std::endl;
        return false;
    }
    
    const Json::Value& results = root["results"];
    int inserted_count = 0;
    
    for (const Json::Value& record : results) {
        if (db_manager.insertNDCRecord(record)) {
            inserted_count++;
        }
    }
    
    std::cout << "Successfully inserted " << inserted_count << " records" << std::endl;
    return true;
}

void DataProcessor::processAllData() {
    // Handle bulk downloads with unified logic
    if (data_source == DataSource::NDC_BULK || 
        data_source == DataSource::DRUGSFDA_BULK || 
        data_source == DataSource::DRUG_LABEL_BULK ||
        data_source == DataSource::FDA_NDC_BULK) {
        
        const DataSourceConfig& config = DataSourceRegistry::getConfig(data_source);
        processBulkDownload(config);
        return;
    }
    
    // Handle NDC API processing (original paginated approach)
    if (data_source == DataSource::NDC_API) {
        std::cout << "🚀 Starting NDC API data processing..." << std::endl;
        
        const int batch_size = 1000;
        int skip = 0;
        int total_processed = 0;
        
        while (true) {
            std::cout << "📥 Downloading batch starting at record " << skip << std::endl;
            
            std::string json_data = downloadNDCData(batch_size, skip);
            if (json_data.empty()) {
                std::cerr << "❌ Failed to download data" << std::endl;
                break;
            }
            
            // Parse to check if we have results
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            Json::Value root;
            std::string errors;
            
            if (!reader->parse(json_data.c_str(), json_data.c_str() + json_data.size(), &root, &errors)) {
                std::cerr << "Error parsing JSON: " << errors << std::endl;
                delete reader;
                break;
            }
            delete reader;
            
            if (!root.isMember("results") || !root["results"].isArray() || root["results"].empty()) {
                std::cout << "✅ No more data available. Processing completed." << std::endl;
                break;
            }
            
            int batch_records = root["results"].size();
            
            if (!parseAndInsertData(json_data)) {
                std::cerr << "❌ Failed to process batch" << std::endl;
                break;
            }
            
            total_processed += batch_records;
            skip += batch_size;
            
            std::cout << "✅ Processed " << total_processed << " records so far..." << std::endl;
            
            // If we got less than batch_size records, we've reached the end
            if (batch_records < batch_size) {
                std::cout << "✅ Reached end of data. Total processed: " << total_processed << " records" << std::endl;
                break;
            }
        }
        
        std::cout << "🎉 NDC API data processing completed!" << std::endl;
    }
    
    // Print error summary at the end of processing
    if (error_logger.getErrorCount() > 0 || error_logger.getDuplicateCount() > 0) {
        std::cout << "\n" << std::endl;
        error_logger.printSummary();
    }
}

std::string DataProcessor::extractPrimaryKey(const Json::Value& record) {
    // Extract primary key based on data source
    switch (data_source) {
        case DataSource::NDC_API:
        case DataSource::NDC_BULK:
            return record.get("product_ndc", "unknown").asString();
            
        case DataSource::DRUGSFDA_BULK:
            return record.get("application_number", "unknown").asString();
            
        case DataSource::DRUG_LABEL_BULK:
            if (record.isMember("openfda") && record["openfda"].isMember("product_ndc") 
                && record["openfda"]["product_ndc"].isArray() && !record["openfda"]["product_ndc"].empty()) {
                return record["openfda"]["product_ndc"][0].asString();
            }
            return record.get("set_id", "unknown").asString();
            
        case DataSource::FDA_NDC_BULK:
            return db_manager.formatNDCToStandard(record.get("product_ndc", "unknown").asString());
            
        default:
            return "unknown";
    }
}

std::string DataProcessor::getTableNameForDataSource(DataSource source) {
    switch (source) {
        case DataSource::NDC_API:
        case DataSource::NDC_BULK:
            return "ndc_data";
            
        case DataSource::DRUGSFDA_BULK:
            return "drugsfda_data";
            
        case DataSource::DRUG_LABEL_BULK:
            return "drug_label_data";
            
        case DataSource::FDA_NDC_BULK:
            return "fda_ndc_data";
            
        default:
            return "unknown_table";
    }
}