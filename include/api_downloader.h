#pragma once

#include "types.h"
#include "database.h"
#include "network.h"
#include <json/json.h>
#include <string>
#include <vector>
#include <functional>

class AdvancedTUI;

class APIDownloader {
private:
    NetworkManager& network_manager;
    DatabaseManager& db_manager;
    AdvancedTUI* tui_ptr;
    bool interactive_mode;
    std::function<void(const std::string&)> output_callback;

public:
    APIDownloader(NetworkManager& network, DatabaseManager& db_manager);
    ~APIDownloader() = default;
    
    // Configuration
    void setTUI(AdvancedTUI* tui) { tui_ptr = tui; }
    void setInteractiveMode(bool interactive) { interactive_mode = interactive; }
    void setOutputCallback(std::function<void(const std::string&)> callback) { output_callback = callback; }
    
    // NDC-based API downloaders
    bool downloadDailyMedImprintData(const std::vector<std::string>& ndc_list);
    bool downloadRxNormNDCProperties(const std::vector<std::string>& ndc_list);
    bool downloadSingleNDCImprint(const std::string& ndc);
    bool downloadSingleRxNormProperties(const std::string& ndc);
    
    // Bulk NDC list extraction
    std::vector<std::string> extractNDCListFromDatabase(int limit = 1000);
    
private:
    void outputMessage(const std::string& message);
    void updateProgress(const std::string& title, int current, int total);
    Json::Value parseDailyMedResponse(const std::string& json_data);
    Json::Value parseRxNormResponse(const std::string& json_data);
    std::string buildDailyMedURL(const std::string& ndc);
    std::string buildRxNormURL(const std::string& ndc);
    std::string normalizeNDC(const std::string& ndc);
};