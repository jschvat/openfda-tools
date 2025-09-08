#include "api_downloader.h"
#include "tui.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

APIDownloader::APIDownloader(NetworkManager& network, DatabaseManager& db_manager)
    : network_manager(network), db_manager(db_manager), tui_ptr(nullptr), interactive_mode(false) {
}

void APIDownloader::outputMessage(const std::string& message) {
    if (output_callback) {
        output_callback(message);
    } else if (interactive_mode && tui_ptr) {
        tui_ptr->addFooterMessage(message);
    } else {
        std::cout << message << std::endl;
    }
}

void APIDownloader::updateProgress(const std::string& title, int current, int total) {
    if (interactive_mode && tui_ptr) {
        tui_ptr->updateProgressBar(current, total);
    } else {
        int percent = (total > 0) ? (current * 100) / total : 0;
        std::cout << "📊 " << title << ": " << current << " / " << total << " (" 
                 << percent << "%)" << std::endl;
    }
}

std::string APIDownloader::normalizeNDC(const std::string& ndc) {
    // Remove any hyphens or spaces from NDC
    std::string normalized = ndc;
    normalized.erase(std::remove(normalized.begin(), normalized.end(), '-'), normalized.end());
    normalized.erase(std::remove(normalized.begin(), normalized.end(), ' '), normalized.end());
    return normalized;
}

std::string APIDownloader::buildDailyMedURL(const std::string& ndc) {
    return "https://dailymed.nlm.nih.gov/dailymed/services/v1/ndc/" + normalizeNDC(ndc) + "/imprintdata.json";
}

std::string APIDownloader::buildRxNormURL(const std::string& ndc) {
    return "https://rxnav.nlm.nih.gov/REST/ndc/" + normalizeNDC(ndc) + "/allProperties.json";
}

Json::Value APIDownloader::parseDailyMedResponse(const std::string& json_data) {
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    Json::Value root;
    std::string parse_errors;
    
    if (!reader->parse(json_data.c_str(), json_data.c_str() + json_data.size(), &root, &parse_errors)) {
        outputMessage("❌ Error parsing DailyMed JSON: " + parse_errors);
        delete reader;
        return Json::Value();
    }
    delete reader;
    
    // DailyMed returns an array of imprint data
    if (root.isArray() && !root.empty()) {
        Json::Value normalized_record;
        const Json::Value& first_record = root[0];
        
        // Map DailyMed fields to our schema
        normalized_record["ndc"] = first_record.get("PRODUCT_CODE", "");
        normalized_record["setid"] = first_record.get("SETID", "");
        normalized_record["spl_version"] = first_record.get("SPL_VERSION", "");
        normalized_record["product_name"] = first_record.get("NAME", "");
        normalized_record["product_code"] = first_record.get("PRODUCT_CODE", "");
        normalized_record["color_text"] = first_record.get("COLOR_TEXT", "");
        normalized_record["color_code"] = first_record.get("SPLCOLOR", "");
        normalized_record["imprint"] = first_record.get("SPLIMPRINT", "");
        normalized_record["shape_text"] = first_record.get("SHAPE_TEXT", "");
        normalized_record["shape_code"] = first_record.get("SPLSHAPE", "");
        normalized_record["size_text"] = first_record.get("SPLSIZE", "");
        normalized_record["score"] = first_record.get("SPLSCORE", "");
        normalized_record["symbol"] = first_record.get("SPLSYMBOL", "");
        normalized_record["coating"] = first_record.get("SPLCOATING", "");
        normalized_record["source_api"] = "dailymed";
        normalized_record["raw_data"] = first_record;
        
        return normalized_record;
    }
    
    return Json::Value();
}

Json::Value APIDownloader::parseRxNormResponse(const std::string& json_data) {
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    Json::Value root;
    std::string parse_errors;
    
    if (!reader->parse(json_data.c_str(), json_data.c_str() + json_data.size(), &root, &parse_errors)) {
        outputMessage("❌ Error parsing RxNorm JSON: " + parse_errors);
        delete reader;
        return Json::Value();
    }
    delete reader;
    
    // RxNorm returns NDC properties in a different structure
    if (root.isMember("ndcPropertyList") && root["ndcPropertyList"].isMember("ndcProperty")) {
        const Json::Value& properties = root["ndcPropertyList"]["ndcProperty"];
        
        if (properties.isArray() && !properties.empty()) {
            Json::Value normalized_record;
            const Json::Value& first_property = properties[0];
            
            // Map RxNorm fields to our schema
            normalized_record["ndc"] = first_property.get("ndc", "");
            normalized_record["product_name"] = first_property.get("propertyName", "");
            normalized_record["labeler_name"] = first_property.get("labelerName", "");
            normalized_record["source_api"] = "rxnorm";
            normalized_record["raw_data"] = root;
            
            return normalized_record;
        }
    }
    
    return Json::Value();
}

bool APIDownloader::downloadSingleNDCImprint(const std::string& ndc) {
    std::string url = buildDailyMedURL(ndc);
    outputMessage("⏳ Downloading imprint data for NDC: " + ndc);
    
    std::string response = network_manager.downloadData(url);
    if (response.empty()) {
        outputMessage("❌ Failed to download data for NDC: " + ndc);
        return false;
    }
    
    Json::Value record = parseDailyMedResponse(response);
    if (record.isNull()) {
        outputMessage("⚠️  No imprint data found for NDC: " + ndc);
        return false;
    }
    
    // Insert into database
    if (db_manager.insertNDCImprintRecord(record)) {
        outputMessage("✅ Inserted imprint data for NDC: " + ndc);
        return true;
    } else {
        outputMessage("❌ Failed to insert data for NDC: " + ndc);
        return false;
    }
}

bool APIDownloader::downloadSingleRxNormProperties(const std::string& ndc) {
    std::string url = buildRxNormURL(ndc);
    outputMessage("⏳ Downloading RxNorm properties for NDC: " + ndc);
    
    std::string response = network_manager.downloadData(url);
    if (response.empty()) {
        outputMessage("❌ Failed to download data for NDC: " + ndc);
        return false;
    }
    
    Json::Value record = parseRxNormResponse(response);
    if (record.isNull()) {
        outputMessage("⚠️  No RxNorm data found for NDC: " + ndc);
        return false;
    }
    
    // Insert into database
    if (db_manager.insertNDCImprintRecord(record)) {
        outputMessage("✅ Inserted RxNorm data for NDC: " + ndc);
        return true;
    } else {
        outputMessage("❌ Failed to insert data for NDC: " + ndc);
        return false;
    }
}

std::vector<std::string> APIDownloader::extractNDCListFromDatabase(int limit) {
    std::vector<std::string> ndc_list;
    
    // Query database for existing NDCs from various tables
    outputMessage("📋 Extracting NDC list from database (limit: " + std::to_string(limit) + ")");
    
    // This would need to be implemented with actual database queries
    // For now, return a sample list for testing
    outputMessage("⚠️  NDC extraction not yet implemented - using sample data");
    
    // Sample NDCs for testing
    ndc_list = {
        "0143-9750-01",
        "0143-9751-01", 
        "0143-9752-01",
        "50580-506-30",
        "50580-519-08"
    };
    
    return ndc_list;
}

bool APIDownloader::downloadDailyMedImprintData(const std::vector<std::string>& ndc_list) {
    outputMessage("🚀 Starting DailyMed imprint data download for " + std::to_string(ndc_list.size()) + " NDCs");
    
    if (interactive_mode && tui_ptr) {
        tui_ptr->showProgressBar("Downloading Imprint Data", 0, ndc_list.size());
    }
    
    int success_count = 0;
    int total_count = ndc_list.size();
    
    for (size_t i = 0; i < ndc_list.size(); i++) {
        const std::string& ndc = ndc_list[i];
        
        if (downloadSingleNDCImprint(ndc)) {
            success_count++;
        }
        
        // Update progress
        updateProgress("Downloading Imprint Data", i + 1, total_count);
        
        // Rate limiting - wait between requests to be respectful to the API
        if (i < ndc_list.size() - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    if (interactive_mode && tui_ptr) {
        tui_ptr->hideProgressBar();
    }
    
    outputMessage("✅ DailyMed download completed: " + std::to_string(success_count) + " / " + 
                 std::to_string(total_count) + " successful");
    
    return success_count > 0;
}

bool APIDownloader::downloadRxNormNDCProperties(const std::vector<std::string>& ndc_list) {
    outputMessage("🚀 Starting RxNorm NDC properties download for " + std::to_string(ndc_list.size()) + " NDCs");
    
    if (interactive_mode && tui_ptr) {
        tui_ptr->showProgressBar("Downloading RxNorm Data", 0, ndc_list.size());
    }
    
    int success_count = 0;
    int total_count = ndc_list.size();
    
    for (size_t i = 0; i < ndc_list.size(); i++) {
        const std::string& ndc = ndc_list[i];
        
        if (downloadSingleRxNormProperties(ndc)) {
            success_count++;
        }
        
        // Update progress
        updateProgress("Downloading RxNorm Data", i + 1, total_count);
        
        // Rate limiting - wait between requests to be respectful to the API
        if (i < ndc_list.size() - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    if (interactive_mode && tui_ptr) {
        tui_ptr->hideProgressBar();
    }
    
    outputMessage("✅ RxNorm download completed: " + std::to_string(success_count) + " / " + 
                 std::to_string(total_count) + " successful");
    
    return success_count > 0;
}