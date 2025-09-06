#include "network.h"
#include <iostream>

NetworkManager::NetworkManager() {}

NetworkManager::~NetworkManager() {}

bool NetworkManager::globalInit() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::cerr << "Error: Failed to initialize libcurl" << std::endl;
        return false;
    }
    return true;
}

void NetworkManager::globalCleanup() {
    curl_global_cleanup();
}

std::string NetworkManager::downloadData(const std::string& url) {
    CURL* curl;
    CURLcode res;
    WriteCallback callback;
    
    curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error: Failed to initialize curl" << std::endl;
        return "";
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback::WriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L); // 5 minute timeout
    
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "❌ Error downloading data: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }
    
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    if (response_code != 200) {
        std::cerr << "❌ HTTP Error: " << response_code << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }
    
    curl_easy_cleanup(curl);
    return callback.data;
}

bool NetworkManager::downloadBulkFile(const std::string& url, const std::string& filename) {
    std::cout << "⏳ Downloading: " << url << std::endl;
    
    CURL* curl;
    CURLcode res;
    std::ofstream file(filename, std::ios::binary);
    
    if (!file.is_open()) {
        std::cerr << "❌ Error: Cannot create file " << filename << std::endl;
        return false;
    }
    
    FileWriteCallback callback;
    callback.file = &file;
    
    curl = curl_easy_init();
    if (!curl) {
        std::cerr << "❌ Error: Failed to initialize curl" << std::endl;
        file.close();
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, FileWriteCallback::WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); // No timeout for bulk downloads
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "❌ Error downloading file: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        file.close();
        return false;
    }
    
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    if (response_code != 200) {
        std::cerr << "❌ HTTP Error: " << response_code << std::endl;
        curl_easy_cleanup(curl);
        file.close();
        return false;
    }
    
    curl_easy_cleanup(curl);
    file.close();
    
    std::cout << "✓ Successfully downloaded bulk file" << std::endl;
    return true;
}