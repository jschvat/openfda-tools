#pragma once

#include <string>
#include <fstream>
#include <functional>
#include <curl/curl.h>

class NetworkManager {
private:
    std::function<void(const std::string&)> output_callback;
    
    struct WriteCallback {
        std::string data;
        static size_t WriteData(void* contents, size_t size, size_t nmemb, WriteCallback* callback) {
            size_t total_size = size * nmemb;
            callback->data.append(static_cast<char*>(contents), total_size);
            return total_size;
        }
    };
    
    struct FileWriteCallback {
        std::ofstream* file;
        static size_t WriteToFile(void* contents, size_t size, size_t nmemb, FileWriteCallback* callback) {
            size_t total_size = size * nmemb;
            callback->file->write(static_cast<char*>(contents), total_size);
            return total_size;
        }
    };

public:
    NetworkManager();
    ~NetworkManager();
    
    // HTTP operations
    std::string downloadData(const std::string& url);
    bool downloadBulkFile(const std::string& url, const std::string& filename);
    
    // Output callback for messages
    void setOutputCallback(std::function<void(const std::string&)> callback) { output_callback = callback; }
    
    // Initialize/cleanup
    static bool globalInit();
    static void globalCleanup();

private:
    void outputMessage(const std::string& message);
};