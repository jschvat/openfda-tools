#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <curl/curl.h>
#include <json/json.h>
#include <mysql/mysql.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <fstream>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <termios.h>
#include <ctype.h>
#include <map>
#include <algorithm>
#include <cctype>
#include <ncurses.h>
#include <menu.h>

enum class DataSource {
    NDC_API,
    NDC_BULK,
    DRUGSFDA_BULK,
    DRUG_LABEL_BULK,
    FDA_NDC_BULK
};

struct DatabaseConfig {
    std::string host;
    int port;
    std::string user;
    std::string password;
    std::string database;
    std::string schema;
};

struct DataSourceConfig {
    std::string name;
    std::string base_url;
    std::string file_pattern;
    int file_count;
    bool is_multi_file;
    
    DataSourceConfig(const std::string& n, const std::string& url, const std::string& pattern, int count = 1)
        : name(n), base_url(url), file_pattern(pattern), file_count(count), is_multi_file(count > 1) {}
};

class DataSourceRegistry {
public:
    static const DataSourceConfig& getConfig(DataSource source) {
        static const std::map<DataSource, DataSourceConfig> configs = {
            {DataSource::NDC_BULK, DataSourceConfig(
                "NDC", 
                "https://download.open.fda.gov/drug/ndc/",
                "drug-ndc-0001-of-0001.json.zip"
            )},
            {DataSource::DRUGSFDA_BULK, DataSourceConfig(
                "DrugsFDA",
                "https://download.open.fda.gov/drug/drugsfda/",
                "drug-drugsfda-0001-of-0001.json.zip"
            )},
            {DataSource::DRUG_LABEL_BULK, DataSourceConfig(
                "Drug Label",
                "https://download.open.fda.gov/drug/label/",
                "drug-label-{:04d}-of-{:04d}.json.zip",
                13
            )},
            {DataSource::FDA_NDC_BULK, DataSourceConfig(
                "FDA NDC Data",
                "https://download.open.fda.gov/drug/ndc/",
                "drug-ndc-0001-of-0001.json.zip"
            )}
        };
        return configs.at(source);
    }
};

class OpenFDADownloader {
private:
    MYSQL* mysql_conn;
    DatabaseConfig db_config;
    DataSource data_source;
    std::string temp_dir;
    
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
    OpenFDADownloader(DataSource source = DataSource::NDC_API)
        : mysql_conn(nullptr), data_source(source) {
        temp_dir = "/tmp/openfda_" + std::to_string(getpid());
        mkdir(temp_dir.c_str(), 0755);
    }
    
    ~OpenFDADownloader() {
        if (mysql_conn) {
            mysql_close(mysql_conn);
        }
        system(("rm -rf " + temp_dir).c_str());
    }
    
    bool loadDatabaseConfig(const std::string& config_file);
    bool initializeDatabase();
    bool ensureSchemaExists();
    bool createNDCTable();
    bool createDrugsFDATable();
    bool createDrugLabelTable();
    bool createFDANDCDataTable();
    std::string formatNDCToStandard(const std::string& ndc);
    std::string downloadNDCData(int limit = 1000, int skip = 0);
    bool downloadBulkFile(const std::string& url, const std::string& filename);
    bool extractZipFile(const std::string& zip_filename);
    bool processBulkJsonFile(const std::string& json_filename);
    bool parseAndInsertData(const std::string& json_data);
    bool clearDatabase();
    void processAllData();
    
private:
    std::string escapeString(const std::string& input);
    bool insertNDCRecord(const Json::Value& record);
    bool insertDrugsFDARecord(const Json::Value& record);
    bool insertDrugLabelRecord(const Json::Value& record);
    bool insertFDANDCRecord(const Json::Value& record);
    
    // Helper functions
    bool tableExists(const std::string& tableName);
    bool clearTable(const std::string& tableName);
    std::string getArrayAsString(const Json::Value& array);
    std::string formatDate(const std::string& dateStr);
    bool processBulkDownload(const DataSourceConfig& config);
    std::string buildUrl(const DataSourceConfig& config, int part = 1);
    bool isDuplicateKeyError(const std::string& error_msg);
    std::string getFirstElement(const Json::Value& parent, const std::string& key);
};

bool OpenFDADownloader::loadDatabaseConfig(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open config file " << config_file << std::endl;
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        size_t pos = line.find(':');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        if (key == "host") {
            db_config.host = value;
        } else if (key == "port") {
            db_config.port = std::stoi(value);
        } else if (key == "user") {
            db_config.user = value;
        } else if (key == "password") {
            db_config.password = value;
        } else if (key == "database") {
            db_config.database = value;
        } else if (key == "schema") {
            db_config.schema = value;
        }
    }
    
    file.close();
    
    // Validate required fields
    if (db_config.host.empty() || db_config.user.empty() || 
        db_config.database.empty() || db_config.schema.empty()) {
        std::cerr << "Error: Missing required database configuration fields" << std::endl;
        return false;
    }
    
    if (db_config.port == 0) {
        db_config.port = 3306; // Default MySQL port
    }
    
    std::cout << "✓ Loaded database configuration:" << std::endl;
    std::cout << "  Host: " << db_config.host << ":" << db_config.port << std::endl;
    std::cout << "  Database: " << db_config.database << std::endl;
    std::cout << "  Schema: " << db_config.schema << std::endl;
    
    return true;
}

bool OpenFDADownloader::initializeDatabase() {
    std::cout << "⏳ Initializing MySQL connection..." << std::endl;
    
    mysql_conn = mysql_init(nullptr);
    if (!mysql_conn) {
        std::cerr << "❌ Error: Failed to initialize MySQL connection" << std::endl;
        return false;
    }
    
    // First connect without specifying database to check/create it
    if (!mysql_real_connect(mysql_conn, db_config.host.c_str(), db_config.user.c_str(), 
                           db_config.password.c_str(), nullptr, db_config.port, nullptr, 0)) {
        std::cerr << "❌ Error: Failed to connect to MySQL server: " 
                  << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    std::cout << "✓ Connected to MySQL server" << std::endl;
    return true;
}

bool OpenFDADownloader::ensureSchemaExists() {
    std::cout << "⏳ Checking database and schema..." << std::endl;
    
    // Check if database exists, create if not
    std::string create_db_query = "CREATE DATABASE IF NOT EXISTS `" + db_config.database + 
                                  "` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci";
    
    if (mysql_query(mysql_conn, create_db_query.c_str())) {
        std::cerr << "❌ Error creating database: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    // Switch to the database
    if (mysql_select_db(mysql_conn, db_config.database.c_str())) {
        std::cerr << "❌ Error selecting database: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    std::cout << "✓ Database '" << db_config.database << "' ready" << std::endl;
    
    // For MySQL, schema and database are the same thing, so we're already using the schema
    // If you need actual schema support (like PostgreSQL), additional logic would go here
    
    return true;
}

bool OpenFDADownloader::createNDCTable() {
    std::cout << "⏳ Creating NDC data table..." << std::endl;
    
    const char* create_table_query = R"(
        CREATE TABLE IF NOT EXISTS ndc_data (
            id INT AUTO_INCREMENT PRIMARY KEY,
            product_ndc VARCHAR(50),
            generic_name LONGTEXT,
            brand_name LONGTEXT,
            labeler_name LONGTEXT,
            product_type VARCHAR(100),
            dosage_form VARCHAR(100),
            route VARCHAR(255),
            marketing_start_date DATE,
            marketing_end_date DATE,
            product_id VARCHAR(100),
            application_number VARCHAR(50),
            brand_name_base VARCHAR(255),
            brand_name_suffix VARCHAR(255),
            active_ingredients JSON,
            packaging JSON,
            openfda_data JSON,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            UNIQUE KEY uk_product_ndc (product_ndc),
            INDEX idx_brand_name (brand_name(100)),
            INDEX idx_labeler_name (labeler_name(100))
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    )";
    
    if (mysql_query(mysql_conn, create_table_query)) {
        std::cerr << "❌ Error creating NDC table: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    std::cout << "✓ NDC table ready" << std::endl;
    return true;
}

bool OpenFDADownloader::createDrugsFDATable() {
    std::cout << "⏳ Creating DrugsFDA data table..." << std::endl;
    
    const char* create_table_query = R"(
        CREATE TABLE IF NOT EXISTS drugsfda_data (
            id INT AUTO_INCREMENT PRIMARY KEY,
            application_number VARCHAR(50),
            sponsor_name TEXT,
            submissions JSON,
            products JSON,
            openfda_data JSON,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_application_number (application_number),
            INDEX idx_sponsor_name (sponsor_name(100))
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    )";
    
    if (mysql_query(mysql_conn, create_table_query)) {
        std::cerr << "❌ Error creating DrugsFDA table: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    std::cout << "✓ DrugsFDA table ready" << std::endl;
    return true;
}

bool OpenFDADownloader::createDrugLabelTable() {
    std::cout << "⏳ Creating Drug Label data table..." << std::endl;
    
    const char* create_table_query = R"(
        CREATE TABLE IF NOT EXISTS drug_label_data (
            id INT AUTO_INCREMENT PRIMARY KEY,
            set_id VARCHAR(100),
            effective_time DATE,
            version VARCHAR(20),
            id_number VARCHAR(100),
            spl_product_data_elements LONGTEXT,
            product_ndc VARCHAR(50),
            generic_name LONGTEXT,
            brand_name LONGTEXT,
            brand_name_base LONGTEXT,
            brand_name_suffix LONGTEXT,
            labeler_name LONGTEXT,
            substance_name LONGTEXT,
            active_ingredient LONGTEXT,
            finished LONGTEXT,
            packaging LONGTEXT,
            listing_expiration_date DATE,
            openfda_application_number LONGTEXT,
            openfda_brand_name LONGTEXT,
            openfda_generic_name LONGTEXT,
            openfda_manufacturer_name LONGTEXT,
            openfda_product_ndc LONGTEXT,
            openfda_product_type LONGTEXT,
            openfda_route LONGTEXT,
            openfda_substance_name LONGTEXT,
            openfda_rxcui LONGTEXT,
            openfda_spl_id LONGTEXT,
            openfda_spl_set_id LONGTEXT,
            openfda_package_ndc LONGTEXT,
            openfda_nui LONGTEXT,
            openfda_pharm_class_moa LONGTEXT,
            openfda_pharm_class_cs LONGTEXT,
            openfda_pharm_class_pe LONGTEXT,
            openfda_pharm_class_epc LONGTEXT,
            openfda_unii LONGTEXT,
            purpose LONGTEXT,
            indications_and_usage LONGTEXT,
            contraindications LONGTEXT,
            description LONGTEXT,
            clinical_pharmacology LONGTEXT,
            warnings LONGTEXT,
            precautions LONGTEXT,
            adverse_reactions LONGTEXT,
            drug_interactions LONGTEXT,
            dosage_and_administration LONGTEXT,
            overdosage LONGTEXT,
            clinical_studies LONGTEXT,
            how_supplied LONGTEXT,
            storage_and_handling LONGTEXT,
            information_for_patients LONGTEXT,
            warnings_and_cautions LONGTEXT,
            pregnancy LONGTEXT,
            pediatric_use LONGTEXT,
            geriatric_use LONGTEXT,
            nursing_mothers LONGTEXT,
            carcinogenesis_and_mutagenesis_and_impairment_of_fertility LONGTEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_set_id (set_id),
            UNIQUE KEY uk_product_ndc (product_ndc),
            INDEX idx_brand_name (brand_name(100)),
            INDEX idx_generic_name (generic_name(100)),
            INDEX idx_labeler_name (labeler_name(100))
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    )";
    
    if (mysql_query(mysql_conn, create_table_query)) {
        std::cerr << "❌ Error creating Drug Label table: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    std::cout << "✓ Drug Label table ready" << std::endl;
    return true;
}

bool OpenFDADownloader::createFDANDCDataTable() {
    const char* create_table_query = R"(
        CREATE TABLE IF NOT EXISTS fda_ndc_data (
            id INT AUTO_INCREMENT PRIMARY KEY,
            product_ndc VARCHAR(13) NOT NULL,
            manufacturer_name VARCHAR(500),
            unii VARCHAR(100),
            product_type VARCHAR(100),
            spl_set_id VARCHAR(100),
            route TEXT,
            generic_name VARCHAR(500),
            brand_name VARCHAR(500),
            substance_name TEXT,
            spl_id VARCHAR(100),
            package_ndc TEXT,
            application_number VARCHAR(100),
            rxcui VARCHAR(100),
            pharm_class_moa TEXT,
            pharm_class_epc TEXT,
            pharm_class_cs TEXT,
            nui VARCHAR(100),
            pharm_class_pe TEXT,
            dosage_form VARCHAR(200),
            is_original_packager BOOLEAN DEFAULT FALSE,
            original_packager_product_ndc VARCHAR(13),
            upc TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            UNIQUE KEY uk_product_ndc (product_ndc),
            INDEX idx_manufacturer_name (manufacturer_name(100)),
            INDEX idx_brand_name (brand_name(100)),
            INDEX idx_generic_name (generic_name(100)),
            INDEX idx_substance_name (substance_name(100))
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    )";
    
    if (mysql_query(mysql_conn, create_table_query)) {
        std::cerr << "❌ Error creating FDA NDC Data table: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    std::cout << "✓ FDA NDC Data table ready" << std::endl;
    return true;
}

std::string OpenFDADownloader::formatNDCToStandard(const std::string& ndc) {
    if (ndc.empty()) return "";
    
    // Remove any non-alphanumeric characters except hyphens
    std::string clean_ndc = ndc;
    clean_ndc.erase(std::remove_if(clean_ndc.begin(), clean_ndc.end(), 
        [](char c) { return !std::isalnum(c) && c != '-'; }), clean_ndc.end());
    
    // Remove existing hyphens to work with raw digits
    std::string digits_only;
    for (char c : clean_ndc) {
        if (std::isdigit(c)) {
            digits_only += c;
        }
    }
    
    // NDC codes should be 10 or 11 digits
    if (digits_only.length() < 10 || digits_only.length() > 11) {
        return ndc; // Return original if invalid format
    }
    
    // Pad to 11 digits if needed (leading zero)
    if (digits_only.length() == 10) {
        digits_only = "0" + digits_only;
    }
    
    // Format as 5-4-2 (00000-0000-00)
    if (digits_only.length() == 11) {
        return digits_only.substr(0, 5) + "-" + 
               digits_only.substr(5, 4) + "-" + 
               digits_only.substr(9, 2);
    }
    
    return ndc; // Return original if we can't format
}

std::string OpenFDADownloader::downloadNDCData(int limit, int skip) {
    CURL* curl;
    CURLcode res;
    WriteCallback callback;
    
    curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error: Failed to initialize CURL" << std::endl;
        return "";
    }
    
    std::stringstream url_stream;
    url_stream << "https://api.fda.gov/drug/ndc.json?limit=" << limit << "&skip=" << skip;
    std::string url = url_stream.str();
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback::WriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "Error downloading data: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }
    
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    curl_easy_cleanup(curl);
    
    if (response_code != 200) {
        std::cerr << "Error: HTTP response code " << response_code << std::endl;
        return "";
    }
    
    std::cout << "Downloaded " << callback.data.size() << " bytes of data" << std::endl;
    return callback.data;
}

bool OpenFDADownloader::downloadBulkFile(const std::string& url, const std::string& filename) {
    CURL* curl;
    CURLcode res;
    FileWriteCallback callback;
    std::ofstream output_file(filename, std::ios::binary);
    
    if (!output_file.is_open()) {
        std::cerr << "Error: Cannot create file " << filename << std::endl;
        return false;
    }
    
    callback.file = &output_file;
    
    curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error: Failed to initialize CURL" << std::endl;
        output_file.close();
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, FileWriteCallback::WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    
    std::cout << "⏳ Downloading bulk file from: " << url << std::endl;
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "Error downloading file: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        output_file.close();
        return false;
    }
    
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    curl_easy_cleanup(curl);
    output_file.close();
    
    if (response_code != 200) {
        std::cerr << "❌ Error: HTTP response code " << response_code << std::endl;
        return false;
    }
    
    std::cout << "✓ Successfully downloaded bulk file" << std::endl;
    return true;
}

bool OpenFDADownloader::extractZipFile(const std::string& zip_filename) {
    std::cout << "⏳ Extracting ZIP file..." << std::endl;
    
    // Use quotes around filenames to handle spaces properly
    std::string command = "cd \"" + temp_dir + "\" && unzip -o \"" + zip_filename + "\"";
    int result = system(command.c_str());
    
    if (result != 0) {
        std::cerr << "❌ Error: Failed to extract ZIP file " << zip_filename << std::endl;
        return false;
    }
    
    std::cout << "✓ ZIP file extracted successfully" << std::endl;
    return true;
}

bool OpenFDADownloader::processBulkJsonFile(const std::string& json_filename) {
    std::cout << "⏳ Loading JSON file..." << std::endl;
    
    std::ifstream json_file(json_filename);
    if (!json_file.is_open()) {
        std::cerr << "❌ Error: Cannot open JSON file " << json_filename << std::endl;
        return false;
    }
    
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    Json::Value root;
    std::string errors;
    
    std::cout << "⏳ Reading JSON content into memory..." << std::endl;
    std::string json_content((std::istreambuf_iterator<char>(json_file)),
                             std::istreambuf_iterator<char>());
    json_file.close();
    
    std::cout << "⏳ Parsing JSON data..." << std::endl;
    if (!reader->parse(json_content.c_str(), json_content.c_str() + json_content.size(), &root, &errors)) {
        std::cerr << "❌ Error parsing JSON file: " << errors << std::endl;
        delete reader;
        return false;
    }
    delete reader;
    
    if (!root.isMember("results") || !root["results"].isArray()) {
        std::cerr << "❌ Error: Invalid JSON structure - missing results array" << std::endl;
        return false;
    }
    
    const Json::Value& results = root["results"];
    int processed_count = 0;
    int duplicate_count = 0;
    int total_records = results.size();
    
    std::cout << "✓ Found " << total_records << " records to process" << std::endl;
    std::cout << "⏳ Inserting data into database..." << std::endl;
    
    for (int i = 0; i < total_records; i++) {
        const Json::Value& record = results[i];
        bool is_duplicate = false;
        
        // Capture cout to detect duplicate messages
        std::streambuf* old_cout = std::cout.rdbuf();
        std::ostringstream capture_stream;
        std::cout.rdbuf(capture_stream.rdbuf());
        
        bool success = false;
        if (data_source == DataSource::DRUGSFDA_BULK) {
            success = insertDrugsFDARecord(record);
        } else if (data_source == DataSource::DRUG_LABEL_BULK) {
            success = insertDrugLabelRecord(record);
        } else if (data_source == DataSource::FDA_NDC_BULK) {
            success = insertFDANDCRecord(record);
        } else {
            success = insertNDCRecord(record);
        }
        
        // Restore cout
        std::cout.rdbuf(old_cout);
        std::string captured_output = capture_stream.str();
        
        if (success) {
            processed_count++;
            if (captured_output.find("Record already exists") != std::string::npos) {
                duplicate_count++;
            }
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

bool OpenFDADownloader::insertDrugLabelRecord(const Json::Value& record) {
    // Helper to safely get first array element
    auto getFirstElement = [this](const Json::Value& parent, const std::string& key) -> std::string {
        if (parent.isMember(key) && parent[key].isArray() && !parent[key].empty()) {
            return parent[key][0].asString();
        }
        return "";
    };
    
    // Helper to format date or return NULL
    auto formatDateOrNull = [this](const std::string& dateStr) -> std::string {
        std::string formatted = formatDate(dateStr);
        return formatted.empty() ? "NULL" : escapeString(formatted);
    };
    
    std::stringstream query;
    query << "INSERT INTO drug_label_data (";
    query << "set_id, effective_time, version, id_number, spl_product_data_elements, ";
    query << "product_ndc, generic_name, brand_name, brand_name_base, brand_name_suffix, ";
    query << "labeler_name, substance_name, active_ingredient, finished, packaging, ";
    query << "listing_expiration_date, openfda_application_number, openfda_brand_name, ";
    query << "openfda_generic_name, openfda_manufacturer_name, openfda_product_ndc, ";
    query << "openfda_product_type, openfda_route, openfda_substance_name, openfda_rxcui, ";
    query << "openfda_spl_id, openfda_spl_set_id, openfda_package_ndc, openfda_nui, ";
    query << "openfda_pharm_class_moa, openfda_pharm_class_cs, openfda_pharm_class_pe, ";
    query << "openfda_pharm_class_epc, openfda_unii, purpose, indications_and_usage, ";
    query << "contraindications, description, clinical_pharmacology, warnings, precautions, ";
    query << "adverse_reactions, drug_interactions, dosage_and_administration, overdosage, ";
    query << "clinical_studies, how_supplied, storage_and_handling, information_for_patients, ";
    query << "warnings_and_cautions, pregnancy, pediatric_use, geriatric_use, nursing_mothers, ";
    query << "carcinogenesis_and_mutagenesis_and_impairment_of_fertility";
    query << ") VALUES (";
    
    // Basic fields
    query << escapeString(record.get("set_id", "").asString()) << ", ";
    query << formatDateOrNull(record.get("effective_time", "").asString()) << ", ";
    query << escapeString(record.get("version", "").asString()) << ", ";
    query << escapeString(record.get("id", "").asString()) << ", ";
    query << escapeString(getArrayAsString(record["spl_product_data_elements"])) << ", ";
    
    // Product and OpenFDA fields
    const Json::Value& openfda = record["openfda"];
    query << escapeString(getFirstElement(openfda, "product_ndc")) << ", ";
    query << escapeString(getArrayAsString(openfda["generic_name"])) << ", ";
    query << escapeString(getArrayAsString(openfda["brand_name"])) << ", ";
    query << escapeString(getArrayAsString(openfda["brand_name_base"])) << ", ";
    query << escapeString(getArrayAsString(openfda["brand_name_suffix"])) << ", ";
    query << escapeString(getArrayAsString(openfda["manufacturer_name"])) << ", ";
    query << escapeString(getArrayAsString(openfda["substance_name"])) << ", ";
    query << escapeString(getArrayAsString(record["active_ingredient"])) << ", ";
    query << escapeString(getArrayAsString(record["finished"])) << ", ";
    query << escapeString(getArrayAsString(record["packaging"])) << ", ";
    query << formatDateOrNull(getArrayAsString(record["listing_expiration_date"])) << ", ";
    
    // More OpenFDA fields (using array of field names to reduce repetition)
    std::vector<std::string> openfda_fields = {
        "application_number", "brand_name", "generic_name", "manufacturer_name", 
        "product_ndc", "product_type", "route", "substance_name", "rxcui",
        "spl_id", "spl_set_id", "package_ndc", "nui", "pharm_class_moa",
        "pharm_class_cs", "pharm_class_pe", "pharm_class_epc", "unii"
    };
    
    for (const auto& field : openfda_fields) {
        query << escapeString(getArrayAsString(openfda[field])) << ", ";
    }
    
    // Label content fields
    std::vector<std::string> label_fields = {
        "purpose", "indications_and_usage", "contraindications", "description",
        "clinical_pharmacology", "warnings", "precautions", "adverse_reactions",
        "drug_interactions", "dosage_and_administration", "overdosage",
        "clinical_studies", "how_supplied", "storage_and_handling",
        "information_for_patients", "warnings_and_cautions", "pregnancy",
        "pediatric_use", "geriatric_use", "nursing_mothers"
    };
    
    for (size_t i = 0; i < label_fields.size(); i++) {
        query << escapeString(getArrayAsString(record[label_fields[i]]));
        if (i < label_fields.size() - 1) query << ", ";
    }
    
    // Last field (no comma)
    query << ", " << escapeString(getArrayAsString(record["carcinogenesis_and_mutagenesis_and_impairment_of_fertility"]));
    query << ")";
    
    if (mysql_query(mysql_conn, query.str().c_str())) {
        std::string error_msg = mysql_error(mysql_conn);
        if (isDuplicateKeyError(error_msg)) {
            std::string product_ndc = getFirstElement(record["openfda"], "product_ndc");
            std::cout << "ℹ️  Record already exists (product_ndc: " << product_ndc << ")" << std::endl;
            return true; // Consider duplicate as success to continue processing
        } else {
            std::cerr << "Error inserting drug label record: " << error_msg << std::endl;
            return false;
        }
    }
    
    return true;
}

bool OpenFDADownloader::insertFDANDCRecord(const Json::Value& record) {
    // Helper to safely get first array element or string value
    auto getFirstElement = [this](const Json::Value& parent, const std::string& key) -> std::string {
        if (!parent.isMember(key)) return "";
        
        const Json::Value& value = parent[key];
        if (value.isArray() && !value.empty()) {
            return value[0].asString();
        } else if (value.isString()) {
            return value.asString();
        }
        return "";
    };
    
    // Get and format product_ndc
    std::string product_ndc = getFirstElement(record, "product_ndc");
    std::string formatted_ndc = formatNDCToStandard(product_ndc);
    
    if (formatted_ndc.empty()) {
        std::cout << "⚠️  Skipping record with invalid product_ndc: " << product_ndc << std::endl;
        return true; // Skip but don't fail the entire process
    }
    
    std::stringstream query;
    query << "INSERT INTO fda_ndc_data (";
    query << "product_ndc, manufacturer_name, unii, product_type, spl_set_id, route, ";
    query << "generic_name, brand_name, substance_name, spl_id, package_ndc, ";
    query << "application_number, rxcui, pharm_class_moa, pharm_class_epc, ";
    query << "pharm_class_cs, nui, pharm_class_pe, dosage_form, ";
    query << "is_original_packager, original_packager_product_ndc, upc";
    query << ") VALUES (";
    
    query << escapeString(formatted_ndc) << ", ";
    query << escapeString(getArrayAsString(record["labeler_name"])) << ", ";
    query << escapeString(getFirstElement(record, "unii")) << ", ";
    query << escapeString(getFirstElement(record, "product_type")) << ", ";
    query << escapeString(getFirstElement(record, "spl_set_id")) << ", ";
    query << escapeString(getArrayAsString(record["route"])) << ", ";
    query << escapeString(getArrayAsString(record["generic_name"])) << ", ";
    query << escapeString(getArrayAsString(record["brand_name"])) << ", ";
    query << escapeString(getArrayAsString(record["substance_name"])) << ", ";
    query << escapeString(getFirstElement(record, "spl_id")) << ", ";
    query << escapeString(getArrayAsString(record["package_ndc"])) << ", ";
    query << escapeString(getFirstElement(record, "application_number")) << ", ";
    query << escapeString(getFirstElement(record, "rxcui")) << ", ";
    query << escapeString(getArrayAsString(record["pharm_class_moa"])) << ", ";
    query << escapeString(getArrayAsString(record["pharm_class_epc"])) << ", ";
    query << escapeString(getArrayAsString(record["pharm_class_cs"])) << ", ";
    query << escapeString(getFirstElement(record, "nui")) << ", ";
    query << escapeString(getArrayAsString(record["pharm_class_pe"])) << ", ";
    query << escapeString(getFirstElement(record, "dosage_form")) << ", ";
    
    // Handle is_original_packager boolean
    std::string is_orig_packager = getFirstElement(record, "is_original_packager");
    query << (is_orig_packager == "true" || is_orig_packager == "1" ? "TRUE" : "FALSE") << ", ";
    
    query << escapeString(formatNDCToStandard(getFirstElement(record, "original_packager_product_ndc"))) << ", ";
    query << escapeString(getArrayAsString(record["upc"]));
    
    query << ")";
    
    if (mysql_query(mysql_conn, query.str().c_str())) {
        std::string error_msg = mysql_error(mysql_conn);
        if (isDuplicateKeyError(error_msg)) {
            std::cout << "ℹ️  Record already exists (product_ndc: " << formatted_ndc << ")" << std::endl;
            return true; // Consider duplicate as success to continue processing
        } else {
            std::cerr << "Error inserting FDA NDC record: " << error_msg << std::endl;
            return false;
        }
    }
    
    return true;
}

std::string OpenFDADownloader::escapeString(const std::string& input) {
    if (input.empty()) return "NULL";
    
    char* escaped = new char[input.length() * 2 + 1];
    mysql_real_escape_string(mysql_conn, escaped, input.c_str(), input.length());
    std::string result = "'" + std::string(escaped) + "'";
    delete[] escaped;
    return result;
}

bool OpenFDADownloader::insertNDCRecord(const Json::Value& record) {
    std::stringstream query;
    query << "INSERT INTO ndc_data (";
    query << "product_ndc, generic_name, brand_name, labeler_name, product_type, ";
    query << "dosage_form, route, marketing_start_date, marketing_end_date, ";
    query << "product_id, application_number, brand_name_base, brand_name_suffix, ";
    query << "active_ingredients, packaging, openfda_data";
    query << ") VALUES (";
    
    query << escapeString(record.get("product_ndc", "").asString()) << ", ";
    query << escapeString(record.get("generic_name", "").asString()) << ", ";
    query << escapeString(record.get("brand_name", "").asString()) << ", ";
    query << escapeString(record.get("labeler_name", "").asString()) << ", ";
    query << escapeString(record.get("product_type", "").asString()) << ", ";
    query << escapeString(record.get("dosage_form", "").asString()) << ", ";
    
    if (record.isMember("route") && record["route"].isArray() && !record["route"].empty()) {
        query << escapeString(record["route"][0].asString()) << ", ";
    } else {
        query << "NULL, ";
    }
    
    std::string marketing_start = record.get("marketing_start_date", "").asString();
    if (!marketing_start.empty() && marketing_start.length() >= 8) {
        std::string formatted_date = marketing_start.substr(0, 4) + "-" + 
                                   marketing_start.substr(4, 2) + "-" + 
                                   marketing_start.substr(6, 2);
        query << escapeString(formatted_date) << ", ";
    } else {
        query << "NULL, ";
    }
    
    std::string marketing_end = record.get("marketing_end_date", "").asString();
    if (!marketing_end.empty() && marketing_end.length() >= 8) {
        std::string formatted_date = marketing_end.substr(0, 4) + "-" + 
                                   marketing_end.substr(4, 2) + "-" + 
                                   marketing_end.substr(6, 2);
        query << escapeString(formatted_date) << ", ";
    } else {
        query << "NULL, ";
    }
    
    query << escapeString(record.get("product_id", "").asString()) << ", ";
    query << escapeString(record.get("application_number", "").asString()) << ", ";
    query << escapeString(record.get("brand_name_base", "").asString()) << ", ";
    query << escapeString(record.get("brand_name_suffix", "").asString()) << ", ";
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    
    if (record.isMember("active_ingredients")) {
        std::ostringstream stream;
        writer->write(record["active_ingredients"], &stream);
        query << escapeString(stream.str()) << ", ";
    } else {
        query << "NULL, ";
    }
    
    if (record.isMember("packaging")) {
        std::ostringstream stream;
        writer->write(record["packaging"], &stream);
        query << escapeString(stream.str()) << ", ";
    } else {
        query << "NULL, ";
    }
    
    if (record.isMember("openfda")) {
        std::ostringstream stream;
        writer->write(record["openfda"], &stream);
        query << escapeString(stream.str());
    } else {
        query << "NULL";
    }
    
    query << ")";
    
    if (mysql_query(mysql_conn, query.str().c_str())) {
        std::string error_msg = mysql_error(mysql_conn);
        if (isDuplicateKeyError(error_msg)) {
            std::cout << "ℹ️  Record already exists (product_ndc: " 
                     << record.get("product_ndc", "").asString() << ")" << std::endl;
            return true; // Consider duplicate as success to continue processing
        } else {
            std::cerr << "Error inserting record: " << error_msg << std::endl;
            return false;
        }
    }
    
    return true;
}

bool OpenFDADownloader::insertDrugsFDARecord(const Json::Value& record) {
    std::stringstream query;
    query << "INSERT INTO drugsfda_data (";
    query << "application_number, sponsor_name, submissions, products, openfda_data";
    query << ") VALUES (";
    
    query << escapeString(record.get("application_number", "").asString()) << ", ";
    query << escapeString(record.get("sponsor_name", "").asString()) << ", ";
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    
    if (record.isMember("submissions")) {
        std::ostringstream stream;
        writer->write(record["submissions"], &stream);
        query << escapeString(stream.str()) << ", ";
    } else {
        query << "NULL, ";
    }
    
    if (record.isMember("products")) {
        std::ostringstream stream;
        writer->write(record["products"], &stream);
        query << escapeString(stream.str()) << ", ";
    } else {
        query << "NULL, ";
    }
    
    if (record.isMember("openfda")) {
        std::ostringstream stream;
        writer->write(record["openfda"], &stream);
        query << escapeString(stream.str());
    } else {
        query << "NULL";
    }
    
    query << ")";
    
    if (mysql_query(mysql_conn, query.str().c_str())) {
        std::cerr << "Error inserting drugsfda record: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    return true;
}

bool OpenFDADownloader::parseAndInsertData(const std::string& json_data) {
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
        if (insertNDCRecord(record)) {
            inserted_count++;
        }
    }
    
    std::cout << "Successfully inserted " << inserted_count << " records" << std::endl;
    return true;
}

bool OpenFDADownloader::tableExists(const std::string& tableName) {
    std::string query = "SHOW TABLES LIKE '" + tableName + "'";
    if (mysql_query(mysql_conn, query.c_str())) {
        return false;
    }
    MYSQL_RES* result = mysql_store_result(mysql_conn);
    if (!result) {
        return false;
    }
    int num_rows = mysql_num_rows(result);
    mysql_free_result(result);
    return num_rows > 0;
}

bool OpenFDADownloader::clearTable(const std::string& tableName) {
    if (!tableExists(tableName)) {
        std::cout << "ℹ️  " << tableName << " table doesn't exist, skipping" << std::endl;
        return true;
    }
    
    std::string clear_query = "DELETE FROM " + tableName;
    if (mysql_query(mysql_conn, clear_query.c_str())) {
        std::cerr << "❌ Error clearing " << tableName << " table: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    std::string reset_query = "ALTER TABLE " + tableName + " AUTO_INCREMENT = 1";
    if (mysql_query(mysql_conn, reset_query.c_str())) {
        std::cerr << "❌ Error resetting " << tableName << " auto increment: " << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    std::cout << "✅ " << tableName << " table cleared" << std::endl;
    return true;
}

std::string OpenFDADownloader::getArrayAsString(const Json::Value& array) {
    if (!array.isArray() || array.empty()) return "";
    std::stringstream ss;
    for (int i = 0; i < static_cast<int>(array.size()); i++) {
        if (i > 0) ss << ", ";
        ss << array[i].asString();
    }
    return ss.str();
}

std::string OpenFDADownloader::formatDate(const std::string& dateStr) {
    if (dateStr.empty() || dateStr.length() < 8) return "";
    return dateStr.substr(0, 4) + "-" + dateStr.substr(4, 2) + "-" + dateStr.substr(6, 2);
}

std::string OpenFDADownloader::buildUrl(const DataSourceConfig& config, int part) {
    if (config.is_multi_file) {
        // Replace {04d} format specifiers with actual zero-padded numbers
        std::string pattern = config.file_pattern;
        std::string url = config.base_url + pattern;
        
        // Find and replace the format specifiers
        size_t pos = url.find("{:04d}");
        if (pos != std::string::npos) {
            char part_str[10];
            snprintf(part_str, sizeof(part_str), "%04d", part);
            url.replace(pos, 6, part_str);  // 6 is length of "{:04d}"
        }
        
        pos = url.find("{:04d}");
        if (pos != std::string::npos) {
            char count_str[10];
            snprintf(count_str, sizeof(count_str), "%04d", config.file_count);
            url.replace(pos, 6, count_str);
        }
        
        return url;
    } else {
        return config.base_url + config.file_pattern;
    }
}

bool OpenFDADownloader::clearDatabase() {
    std::cout << "⏳ Clearing database tables..." << std::endl;
    
    bool success = true;
    
    // Clear only the relevant table(s) based on data source
    switch (data_source) {
        case DataSource::NDC_API:
        case DataSource::NDC_BULK:
            success = clearTable("ndc_data");
            break;
        case DataSource::DRUGSFDA_BULK:
            success = clearTable("drugsfda_data");
            break;
        case DataSource::DRUG_LABEL_BULK:
            success = clearTable("drug_label_data");
            break;
        case DataSource::FDA_NDC_BULK:
            success = clearTable("fda_ndc_data");
            break;
        default:
            // If unknown source, clear all tables as fallback
            success = clearTable("ndc_data") && 
                     clearTable("drugsfda_data") && 
                     clearTable("drug_label_data") &&
                     clearTable("fda_ndc_data");
            break;
    }
    
    if (success) {
        std::cout << "✅ Database clearing completed" << std::endl;
    } else {
        std::cerr << "❌ Failed to clear database table(s)" << std::endl;
    }
    
    return success;
}

bool OpenFDADownloader::isDuplicateKeyError(const std::string& error_msg) {
    // MySQL duplicate key error codes
    return error_msg.find("Duplicate entry") != std::string::npos ||
           error_msg.find("1062") != std::string::npos ||
           error_msg.find("uk_product_ndc") != std::string::npos;
}

std::string OpenFDADownloader::getFirstElement(const Json::Value& parent, const std::string& key) {
    if (parent.isMember(key) && parent[key].isArray() && !parent[key].empty()) {
        return parent[key][0].asString();
    }
    return "";
}

bool OpenFDADownloader::processBulkDownload(const DataSourceConfig& config) {
    std::cout << "🚀 Starting " << config.name << " bulk data processing..." << std::endl;
    
    if (config.is_multi_file) {
        // Multi-file processing (like Drug Labels)
        int successful_parts = 0;
        
        for (int part = 1; part <= config.file_count; part++) {
            std::cout << "📥 Downloading part " << part << " of " << config.file_count << "..." << std::endl;
            
            std::string url = buildUrl(config, part);
            std::stringstream zip_filename_stream;
            // Replace spaces with underscores and use lowercase
            std::string safe_name = config.name;
            std::replace(safe_name.begin(), safe_name.end(), ' ', '_');
            std::transform(safe_name.begin(), safe_name.end(), safe_name.begin(), ::tolower);
            
            zip_filename_stream << temp_dir << "/" << safe_name << "-part-" 
                               << std::setfill('0') << std::setw(4) << part << ".zip";
            std::string zip_filename = zip_filename_stream.str();
            
            if (!downloadBulkFile(url, zip_filename)) {
                std::cerr << "❌ Failed to download part " << part << std::endl;
                continue;
            }
            
            if (!extractZipFile(zip_filename)) {
                std::cerr << "❌ Failed to extract part " << part << std::endl;
                continue;
            }
            
            // Build expected JSON filename from URL pattern
            std::string json_filename = temp_dir + "/" + config.file_pattern;
            
            // Replace format specifiers with actual values
            size_t pos = json_filename.find("{:04d}");
            if (pos != std::string::npos) {
                char part_str[10];
                snprintf(part_str, sizeof(part_str), "%04d", part);
                json_filename.replace(pos, 6, part_str);
            }
            
            pos = json_filename.find("{:04d}");
            if (pos != std::string::npos) {
                char count_str[10];
                snprintf(count_str, sizeof(count_str), "%04d", config.file_count);
                json_filename.replace(pos, 6, count_str);
            }
            
            // Remove .zip extension
            if (json_filename.size() > 4 && json_filename.substr(json_filename.size() - 4) == ".zip") {
                json_filename = json_filename.substr(0, json_filename.size() - 4);
            }
            
            if (!processBulkJsonFile(json_filename)) {
                std::cerr << "❌ Failed to process part " << part << std::endl;
                continue;
            }
            
            successful_parts++;
            int percent = (successful_parts * 100) / config.file_count;
            std::cout << "✅ Part " << part << " completed (" << successful_parts 
                     << "/" << config.file_count << " - " << percent << "%)" << std::endl;
        }
        
        if (successful_parts > 0) {
            std::cout << "🎉 " << config.name << " bulk data processing completed! " 
                     << "Successfully processed " << successful_parts << " out of " 
                     << config.file_count << " parts." << std::endl;
            return true;
        } else {
            std::cerr << "❌ No " << config.name << " parts were successfully processed!" << std::endl;
            return false;
        }
    } else {
        // Single file processing
        std::string url = buildUrl(config);
        std::string safe_name = config.name;
        std::replace(safe_name.begin(), safe_name.end(), ' ', '_');
        std::transform(safe_name.begin(), safe_name.end(), safe_name.begin(), ::tolower);
        std::string zip_filename = temp_dir + "/" + safe_name + "-bulk.zip";
        
        if (!downloadBulkFile(url, zip_filename)) {
            std::cerr << "❌ Failed to download bulk file" << std::endl;
            return false;
        }
        
        if (!extractZipFile(zip_filename)) {
            std::cerr << "❌ Failed to extract ZIP file" << std::endl;
            return false;
        }
        
        std::string json_filename = temp_dir + "/" + config.file_pattern;
        // Remove .zip extension
        if (json_filename.size() > 4 && json_filename.substr(json_filename.size() - 4) == ".zip") {
            json_filename = json_filename.substr(0, json_filename.size() - 4);
        }
        
        if (!processBulkJsonFile(json_filename)) {
            std::cerr << "❌ Failed to process JSON file" << std::endl;
            return false;
        }
        
        std::cout << "🎉 " << config.name << " bulk data processing completed successfully!" << std::endl;
        return true;
    }
}

void OpenFDADownloader::processAllData() {
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
                std::cout << "No more data to download or error occurred" << std::endl;
                break;
            }
            
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            Json::Value root;
            std::string errors;
            
            if (reader->parse(json_data.c_str(), json_data.c_str() + json_data.size(), &root, &errors)) {
                if (root.isMember("results") && root["results"].isArray()) {
                    int batch_count = root["results"].size();
                    if (batch_count == 0) {
                        std::cout << "No more records available" << std::endl;
                        delete reader;
                        break;
                    }
                    
                    parseAndInsertData(json_data);
                    total_processed += batch_count;
                    skip += batch_count;
                    
                    std::cout << "📊 Total records processed: " << total_processed << std::endl;
                    
                    if (batch_count < batch_size) {
                        std::cout << "Reached end of available data" << std::endl;
                        delete reader;
                        break;
                    }
                }
            }
            delete reader;
            
            // Small delay between API requests
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "🎉 NDC API data processing completed. Total records: " << total_processed << std::endl;
    }
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] [config_file]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -n, --ndc         Use NDC API (default)" << std::endl;
    std::cout << "  -b, --ndc-bulk    Use NDC bulk download" << std::endl;
    std::cout << "  -d, --drugsfda    Use DrugsFDA bulk download" << std::endl;
    std::cout << "  -l, --drug-label  Use Drug Label bulk download (13 parts)" << std::endl;
    std::cout << "  -f, --fda-ndc     Use FDA NDC bulk download (normalized format)" << std::endl;
    std::cout << "  -x, --clear       Clear database tables before processing" << std::endl;
    std::cout << "  -i, --interactive Interactive TUI mode" << std::endl;
    std::cout << "  -c, --config      Database config file (default: database.nfo)" << std::endl;
    std::cout << "  -h, --help        Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "The config file should contain database connection parameters:" << std::endl;
    std::cout << "  host: your_mysql_host" << std::endl;
    std::cout << "  port: 3306" << std::endl;
    std::cout << "  user: your_username" << std::endl;
    std::cout << "  password: your_password" << std::endl;
    std::cout << "  database: database_name" << std::endl;
    std::cout << "  schema: schema_name" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << "                    # Uses database.nfo, NDC API" << std::endl;
    std::cout << "  " << program_name << " --ndc-bulk        # NDC bulk download mode" << std::endl;
    std::cout << "  " << program_name << " --drugsfda        # DrugsFDA bulk download mode" << std::endl;
    std::cout << "  " << program_name << " --drug-label      # Drug Label bulk download mode" << std::endl;
    std::cout << "  " << program_name << " --fda-ndc         # FDA NDC bulk download mode (normalized)" << std::endl;
    std::cout << "  " << program_name << " --clear --fda-ndc # Clear database, then FDA NDC bulk" << std::endl;
    std::cout << "  " << program_name << " -c mydb.conf      # Custom config file" << std::endl;
    std::cout << "  " << program_name << " -i                # Interactive TUI mode" << std::endl;
}

class AdvancedTUI {
private:
    WINDOW* main_win;
    WINDOW* menu_win;
    WINDOW* status_win;
    int height, width;
    
public:
    AdvancedTUI() : main_win(nullptr), menu_win(nullptr), status_win(nullptr) {}
    
    ~AdvancedTUI() {
        cleanup();
    }
    
    bool initialize() {
        // Initialize ncurses
        main_win = initscr();
        if (!main_win) return false;
        
        // Get screen dimensions
        getmaxyx(stdscr, height, width);
        
        // Setup ncurses options
        cbreak();           // Line buffering disabled
        noecho();           // Don't echo() while we do getch
        keypad(stdscr, TRUE); // Enable arrow keys
        
        // Initialize colors if supported
        if (has_colors()) {
            start_color();
            init_pair(1, COLOR_WHITE, COLOR_BLUE);   // Header
            init_pair(2, COLOR_YELLOW, COLOR_BLACK); // Highlight
            init_pair(3, COLOR_GREEN, COLOR_BLACK);  // Success
            init_pair(4, COLOR_RED, COLOR_BLACK);    // Error
            init_pair(5, COLOR_CYAN, COLOR_BLACK);   // Info
        }
        
        return true;
    }
    
    void cleanup() {
        if (menu_win) { delwin(menu_win); menu_win = nullptr; }
        if (status_win) { delwin(status_win); status_win = nullptr; }
        if (main_win) { 
            endwin(); 
            main_win = nullptr; 
        }
    }
    
    void drawHeader(const std::string& title) {
        // Draw header
        if (has_colors()) attron(COLOR_PAIR(1));
        attron(A_BOLD);
        
        mvprintw(0, 0, "%*s", width, " "); // Clear line
        mvprintw(0, (width - title.length()) / 2, "%s", title.c_str());
        
        mvprintw(1, 0, "%*s", width, " "); // Clear line
        std::string subtitle = "Interactive Configuration";
        mvprintw(1, (width - subtitle.length()) / 2, "%s", subtitle.c_str());
        
        // Draw separator
        mvprintw(2, 0, "%s", std::string(width, '=').c_str());
        
        attroff(A_BOLD);
        if (has_colors()) attroff(COLOR_PAIR(1));
        
        refresh();
    }
    
    int showMenu(const std::vector<std::string>& options, const std::string& title) {
        clear();
        drawHeader("OpenFDA Data Downloader");
        
        // Calculate menu window dimensions
        int menu_height = options.size() + 4;
        int menu_width = 60;
        int start_y = 5;
        int start_x = (width - menu_width) / 2;
        
        // Create menu window
        menu_win = newwin(menu_height, menu_width, start_y, start_x);
        if (!menu_win) return -1;
        
        keypad(menu_win, TRUE);  // Enable arrow keys for menu window
        box(menu_win, 0, 0);
        
        // Draw title
        if (has_colors()) wattron(menu_win, COLOR_PAIR(2));
        wattron(menu_win, A_BOLD);
        mvwprintw(menu_win, 1, 2, "%s", title.c_str());
        wattroff(menu_win, A_BOLD);
        if (has_colors()) wattroff(menu_win, COLOR_PAIR(2));
        
        mvwhline(menu_win, 2, 1, '-', menu_width - 2);
        
        // Draw options
        for (size_t i = 0; i < options.size(); i++) {
            mvwprintw(menu_win, 3 + i, 2, "%zu. %s", i + 1, options[i].c_str());
        }
        
        wrefresh(menu_win);
        
        // Draw status
        drawStatus("Use number keys (1-" + std::to_string(options.size()) + ") or arrow keys + Enter. Press 'q' to quit.");
        
        // Handle input
        int selected = 0;
        int ch;
        
        while ((ch = wgetch(menu_win)) != 'q') {
            // Clear previous highlight
            mvwprintw(menu_win, 3 + selected, 2, "%d. %s", selected + 1, options[selected].c_str());
            
            switch (ch) {
                case KEY_UP:
                    selected = (selected - 1 + options.size()) % options.size();
                    break;
                case KEY_DOWN:
                    selected = (selected + 1) % options.size();
                    break;
                case '\n':
                case '\r':
                    delwin(menu_win);
                    menu_win = nullptr;
                    return selected + 1;
                case '1': case '2': case '3': case '4': case '5':
                case '6': case '7': case '8': case '9':
                    if (ch - '0' <= static_cast<int>(options.size())) {
                        delwin(menu_win);
                        menu_win = nullptr;
                        return ch - '0';
                    }
                    break;
            }
            
            // Highlight current selection
            if (has_colors()) wattron(menu_win, COLOR_PAIR(2));
            wattron(menu_win, A_REVERSE);
            mvwprintw(menu_win, 3 + selected, 2, "%d. %s", selected + 1, options[selected].c_str());
            wattroff(menu_win, A_REVERSE);
            if (has_colors()) wattroff(menu_win, COLOR_PAIR(2));
            
            wrefresh(menu_win);
        }
        
        delwin(menu_win);
        menu_win = nullptr;
        return -1; // Quit selected
    }
    
    bool showYesNoDialog(const std::string& question) {
        clear();
        drawHeader("Confirmation");
        
        int dialog_height = 6;
        int dialog_width = std::max(50, static_cast<int>(question.length() + 10));
        int start_y = (height - dialog_height) / 2;
        int start_x = (width - dialog_width) / 2;
        
        WINDOW* dialog = newwin(dialog_height, dialog_width, start_y, start_x);
        box(dialog, 0, 0);
        
        mvwprintw(dialog, 2, 2, "%s", question.c_str());
        mvwprintw(dialog, 4, 2, "[Y]es    [N]o");
        
        wrefresh(dialog);
        
        int ch;
        bool result = false;
        
        while ((ch = getch()) != '\n' && ch != '\r') {
            switch (tolower(ch)) {
                case 'y':
                    result = true;
                    goto done;
                case 'n':
                    result = false;
                    goto done;
            }
        }
        
        done:
        delwin(dialog);
        return result;
    }
    
    std::string getStringInput(const std::string& prompt, const std::string& defaultValue = "") {
        clear();
        drawHeader("Input Required");
        
        int dialog_height = 8;
        int dialog_width = 60;
        int start_y = (height - dialog_height) / 2;
        int start_x = (width - dialog_width) / 2;
        
        WINDOW* dialog = newwin(dialog_height, dialog_width, start_y, start_x);
        box(dialog, 0, 0);
        
        mvwprintw(dialog, 2, 2, "%s", prompt.c_str());
        if (!defaultValue.empty()) {
            mvwprintw(dialog, 3, 2, "Default: %s", defaultValue.c_str());
        }
        mvwprintw(dialog, 5, 2, "> ");
        
        wrefresh(dialog);
        
        // Enable echo for text input
        echo();
        
        char buffer[256] = {0};
        wgetnstr(dialog, buffer, sizeof(buffer) - 1);
        
        // Disable echo
        noecho();
        
        delwin(dialog);
        
        std::string result(buffer);
        if (result.empty() && !defaultValue.empty()) {
            return defaultValue;
        }
        
        return result;
    }
    
    void drawStatus(const std::string& message) {
        // Create status window at bottom
        if (status_win) delwin(status_win);
        status_win = newwin(3, width, height - 3, 0);
        
        box(status_win, 0, 0);
        
        if (has_colors()) wattron(status_win, COLOR_PAIR(5));
        mvwprintw(status_win, 1, 2, "%s", message.c_str());
        if (has_colors()) wattroff(status_win, COLOR_PAIR(5));
        
        wrefresh(status_win);
    }
    
    void showConfigSummary(DataSource dataSource, bool clearDb, const std::string& configFile) {
        clear();
        drawHeader("Configuration Summary");
        
        std::string sourceStr;
        switch (dataSource) {
            case DataSource::NDC_API:
                sourceStr = "NDC API (paginated requests)";
                break;
            case DataSource::NDC_BULK:
                sourceStr = "NDC Bulk Download";
                break;
            case DataSource::DRUGSFDA_BULK:
                sourceStr = "DrugsFDA Bulk Download";
                break;
            case DataSource::DRUG_LABEL_BULK:
                sourceStr = "Drug Label Bulk Download (13 parts)";
                break;
            case DataSource::FDA_NDC_BULK:
                sourceStr = "FDA NDC Bulk Download (normalized format)";
                break;
        }
        
        int start_y = 5;
        
        if (has_colors()) attron(COLOR_PAIR(3));
        mvprintw(start_y, 4, "Data Source: %s", sourceStr.c_str());
        mvprintw(start_y + 1, 4, "Clear Database: %s", clearDb ? "Yes" : "No");
        mvprintw(start_y + 2, 4, "Config File: %s", configFile.c_str());
        if (has_colors()) attroff(COLOR_PAIR(3));
        
        drawStatus("Press any key to continue...");
        refresh();
        getch();
    }
};

bool runInteractiveMode(DataSource& dataSource, bool& clearDatabase, std::string& configFile) {
    AdvancedTUI tui;
    
    if (!tui.initialize()) {
        std::cerr << "❌ Failed to initialize TUI. Falling back to simple mode." << std::endl;
        return false;
    }
    
    // Data source selection
    std::vector<std::string> dataSourceOptions = {
        "NDC API (Multiple API requests - slower but always up-to-date)",
        "NDC Bulk Download (Single large file - faster)",
        "DrugsFDA Bulk Download (Single large file - different dataset)",
        "Drug Label Bulk Download (13 large files - comprehensive label data)",
        "FDA NDC Bulk Download (Single large file - normalized NDC format)"
    };
    
    int choice = tui.showMenu(dataSourceOptions, "Select Data Source");
    
    if (choice == -1) {
        tui.cleanup();
        std::cout << "❌ Operation cancelled by user." << std::endl;
        return false;
    }
    
    switch (choice) {
        case 1: dataSource = DataSource::NDC_API; break;
        case 2: dataSource = DataSource::NDC_BULK; break;
        case 3: dataSource = DataSource::DRUGSFDA_BULK; break;
        case 4: dataSource = DataSource::DRUG_LABEL_BULK; break;
        case 5: dataSource = DataSource::FDA_NDC_BULK; break;
    }
    
    // Clear database option
    clearDatabase = tui.showYesNoDialog("Clear existing database tables before import?");
    
    // Config file option
    configFile = tui.getStringInput("Database config file path", "database.nfo");
    
    // Show summary and confirm
    tui.showConfigSummary(dataSource, clearDatabase, configFile);
    
    bool proceed = tui.showYesNoDialog("Proceed with these settings?");
    
    // Clean up TUI
    tui.cleanup();
    
    if (!proceed) {
        std::cout << "❌ Operation cancelled by user." << std::endl;
        return false;
    }
    
    std::cout << std::endl;
    std::cout << "✅ Starting data processing..." << std::endl;
    std::cout << std::endl;
    
    return true;
}

int main(int argc, char* argv[]) {
    DataSource data_source = DataSource::NDC_API;
    std::string config_file = "database.nfo";
    bool clear_database = false;
    bool interactive_mode = false;
    int option_index = 0;
    int c;
    
    struct option long_options[] = {
        {"ndc",         no_argument,       0, 'n'},
        {"ndc-bulk",    no_argument,       0, 'b'},
        {"drugsfda",    no_argument,       0, 'd'},
        {"drug-label",  no_argument,       0, 'l'},
        {"fda-ndc",     no_argument,       0, 'f'},
        {"clear",       no_argument,       0, 'x'},
        {"interactive", no_argument,       0, 'i'},
        {"config",      required_argument, 0, 'c'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((c = getopt_long(argc, argv, "nbdlfxic:h", long_options, &option_index)) != -1) {
        switch (c) {
            case 'n':
                data_source = DataSource::NDC_API;
                break;
            case 'b':
                data_source = DataSource::NDC_BULK;
                break;
            case 'd':
                data_source = DataSource::DRUGSFDA_BULK;
                break;
            case 'l':
                data_source = DataSource::DRUG_LABEL_BULK;
                break;
            case 'f':
                data_source = DataSource::FDA_NDC_BULK;
                break;
            case 'x':
                clear_database = true;
                break;
            case 'i':
                interactive_mode = true;
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            case '?':
                printUsage(argv[0]);
                return 1;
            default:
                break;
        }
    }
    
    // If there's a remaining argument, use it as config file
    if (optind < argc) {
        config_file = argv[optind];
    }
    
    // Run interactive mode if requested
    if (interactive_mode) {
        if (!runInteractiveMode(data_source, clear_database, config_file)) {
            return 1;
        }
    }
    
    std::cout << "🚀 OpenFDA Data Downloader" << std::endl;
    std::cout << "===============================================" << std::endl;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    OpenFDADownloader downloader(data_source);
    
    // Load database configuration
    if (!downloader.loadDatabaseConfig(config_file)) {
        curl_global_cleanup();
        return 1;
    }
    
    // Connect to database
    if (!downloader.initializeDatabase()) {
        curl_global_cleanup();
        return 1;
    }
    
    // Ensure schema exists
    if (!downloader.ensureSchemaExists()) {
        curl_global_cleanup();
        return 1;
    }
    
    // Create appropriate table
    if (data_source == DataSource::NDC_API || data_source == DataSource::NDC_BULK) {
        if (!downloader.createNDCTable()) {
            curl_global_cleanup();
            return 1;
        }
        if (data_source == DataSource::NDC_API) {
            std::cout << "🔄 Processing NDC data from API..." << std::endl;
        } else {
            std::cout << "🔄 Processing NDC data from bulk download..." << std::endl;
        }
    } else if (data_source == DataSource::DRUG_LABEL_BULK) {
        if (!downloader.createDrugLabelTable()) {
            curl_global_cleanup();
            return 1;
        }
        std::cout << "🔄 Processing Drug Label data from bulk download..." << std::endl;
    } else if (data_source == DataSource::FDA_NDC_BULK) {
        if (!downloader.createFDANDCDataTable()) {
            curl_global_cleanup();
            return 1;
        }
        std::cout << "🔄 Processing FDA NDC data from bulk download..." << std::endl;
    } else {
        if (!downloader.createDrugsFDATable()) {
            curl_global_cleanup();
            return 1;
        }
        std::cout << "🔄 Processing DrugsFDA data from bulk download..." << std::endl;
    }
    
    // Clear database if requested
    if (clear_database) {
        if (!downloader.clearDatabase()) {
            curl_global_cleanup();
            return 1;
        }
    }
    
    downloader.processAllData();
    
    curl_global_cleanup();
    return 0;
}