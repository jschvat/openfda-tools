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

enum class DataSource {
    NDC_API,
    NDC_BULK,
    DRUGSFDA_BULK,
    DRUG_LABEL_BULK
};

struct DatabaseConfig {
    std::string host;
    int port;
    std::string user;
    std::string password;
    std::string database;
    std::string schema;
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
            generic_name TEXT,
            brand_name TEXT,
            labeler_name TEXT,
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
            INDEX idx_product_ndc (product_ndc),
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
            spl_product_data_elements TEXT,
            product_ndc VARCHAR(50),
            generic_name TEXT,
            brand_name TEXT,
            brand_name_base TEXT,
            brand_name_suffix TEXT,
            labeler_name TEXT,
            substance_name TEXT,
            active_ingredient TEXT,
            finished TEXT,
            packaging TEXT,
            listing_expiration_date DATE,
            openfda_application_number TEXT,
            openfda_brand_name TEXT,
            openfda_generic_name TEXT,
            openfda_manufacturer_name TEXT,
            openfda_product_ndc TEXT,
            openfda_product_type TEXT,
            openfda_route TEXT,
            openfda_substance_name TEXT,
            openfda_rxcui TEXT,
            openfda_spl_id TEXT,
            openfda_spl_set_id TEXT,
            openfda_package_ndc TEXT,
            openfda_nui TEXT,
            openfda_pharm_class_moa TEXT,
            openfda_pharm_class_cs TEXT,
            openfda_pharm_class_pe TEXT,
            openfda_pharm_class_epc TEXT,
            openfda_unii TEXT,
            purpose TEXT,
            indications_and_usage TEXT,
            contraindications TEXT,
            description TEXT,
            clinical_pharmacology TEXT,
            warnings TEXT,
            precautions TEXT,
            adverse_reactions TEXT,
            drug_interactions TEXT,
            dosage_and_administration TEXT,
            overdosage TEXT,
            clinical_studies TEXT,
            how_supplied TEXT,
            storage_and_handling TEXT,
            information_for_patients TEXT,
            warnings_and_cautions TEXT,
            pregnancy TEXT,
            pediatric_use TEXT,
            geriatric_use TEXT,
            nursing_mothers TEXT,
            carcinogenesis_and_mutagenesis_and_impairment_of_fertility TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_set_id (set_id),
            INDEX idx_product_ndc (product_ndc),
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
    
    std::string command = "cd " + temp_dir + " && unzip -o " + zip_filename;
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
    int inserted_count = 0;
    int total_records = results.size();
    
    std::cout << "✓ Found " << total_records << " records to process" << std::endl;
    std::cout << "⏳ Inserting data into database..." << std::endl;
    
    for (int i = 0; i < total_records; i++) {
        const Json::Value& record = results[i];
        
        if (data_source == DataSource::DRUGSFDA_BULK) {
            if (insertDrugsFDARecord(record)) {
                inserted_count++;
            }
        } else if (data_source == DataSource::DRUG_LABEL_BULK) {
            if (insertDrugLabelRecord(record)) {
                inserted_count++;
            }
        } else {
            if (insertNDCRecord(record)) {
                inserted_count++;
            }
        }
        
        if (i % 1000 == 0 && i > 0) {
            int percent = (i * 100) / total_records;
            std::cout << "📊 Progress: " << i << " / " << total_records << " (" 
                     << percent << "%) - " << inserted_count << " inserted" << std::endl;
        }
    }
    
    std::cout << "✅ Successfully processed " << inserted_count << " / " << total_records 
              << " records from bulk file" << std::endl;
    return true;
}

bool OpenFDADownloader::insertDrugLabelRecord(const Json::Value& record) {
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
    
    // Helper function to get array as comma-separated string
    auto getArrayAsString = [](const Json::Value& array) -> std::string {
        if (!array.isArray() || array.empty()) return "";
        std::stringstream ss;
        for (int i = 0; i < static_cast<int>(array.size()); i++) {
            if (i > 0) ss << ", ";
            ss << array[i].asString();
        }
        return ss.str();
    };
    
    query << escapeString(record.get("set_id", "").asString()) << ", ";
    
    // Handle effective_time date conversion
    std::string effective_time = record.get("effective_time", "").asString();
    if (!effective_time.empty() && effective_time.length() >= 8) {
        std::string formatted_date = effective_time.substr(0, 4) + "-" + 
                                   effective_time.substr(4, 2) + "-" + 
                                   effective_time.substr(6, 2);
        query << escapeString(formatted_date) << ", ";
    } else {
        query << "NULL, ";
    }
    
    query << escapeString(record.get("version", "").asString()) << ", ";
    query << escapeString(record.get("id", "").asString()) << ", ";
    query << escapeString(getArrayAsString(record["spl_product_data_elements"])) << ", ";
    
    // Get first product NDC if available
    std::string product_ndc;
    if (record.isMember("openfda") && record["openfda"].isMember("product_ndc") && 
        record["openfda"]["product_ndc"].isArray() && !record["openfda"]["product_ndc"].empty()) {
        product_ndc = record["openfda"]["product_ndc"][0].asString();
    }
    query << escapeString(product_ndc) << ", ";
    
    query << escapeString(getArrayAsString(record["openfda"]["generic_name"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["brand_name"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["brand_name_base"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["brand_name_suffix"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["manufacturer_name"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["substance_name"])) << ", ";
    query << escapeString(getArrayAsString(record["active_ingredient"])) << ", ";
    query << escapeString(getArrayAsString(record["finished"])) << ", ";
    query << escapeString(getArrayAsString(record["packaging"])) << ", ";
    
    // Handle listing_expiration_date
    std::string exp_date = getArrayAsString(record["listing_expiration_date"]);
    if (!exp_date.empty() && exp_date.length() >= 8) {
        std::string formatted_exp_date = exp_date.substr(0, 4) + "-" + 
                                       exp_date.substr(4, 2) + "-" + 
                                       exp_date.substr(6, 2);
        query << escapeString(formatted_exp_date) << ", ";
    } else {
        query << "NULL, ";
    }
    
    // OpenFDA fields
    query << escapeString(getArrayAsString(record["openfda"]["application_number"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["brand_name"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["generic_name"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["manufacturer_name"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["product_ndc"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["product_type"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["route"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["substance_name"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["rxcui"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["spl_id"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["spl_set_id"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["package_ndc"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["nui"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["pharm_class_moa"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["pharm_class_cs"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["pharm_class_pe"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["pharm_class_epc"])) << ", ";
    query << escapeString(getArrayAsString(record["openfda"]["unii"])) << ", ";
    
    // Label content fields
    query << escapeString(getArrayAsString(record["purpose"])) << ", ";
    query << escapeString(getArrayAsString(record["indications_and_usage"])) << ", ";
    query << escapeString(getArrayAsString(record["contraindications"])) << ", ";
    query << escapeString(getArrayAsString(record["description"])) << ", ";
    query << escapeString(getArrayAsString(record["clinical_pharmacology"])) << ", ";
    query << escapeString(getArrayAsString(record["warnings"])) << ", ";
    query << escapeString(getArrayAsString(record["precautions"])) << ", ";
    query << escapeString(getArrayAsString(record["adverse_reactions"])) << ", ";
    query << escapeString(getArrayAsString(record["drug_interactions"])) << ", ";
    query << escapeString(getArrayAsString(record["dosage_and_administration"])) << ", ";
    query << escapeString(getArrayAsString(record["overdosage"])) << ", ";
    query << escapeString(getArrayAsString(record["clinical_studies"])) << ", ";
    query << escapeString(getArrayAsString(record["how_supplied"])) << ", ";
    query << escapeString(getArrayAsString(record["storage_and_handling"])) << ", ";
    query << escapeString(getArrayAsString(record["information_for_patients"])) << ", ";
    query << escapeString(getArrayAsString(record["warnings_and_cautions"])) << ", ";
    query << escapeString(getArrayAsString(record["pregnancy"])) << ", ";
    query << escapeString(getArrayAsString(record["pediatric_use"])) << ", ";
    query << escapeString(getArrayAsString(record["geriatric_use"])) << ", ";
    query << escapeString(getArrayAsString(record["nursing_mothers"])) << ", ";
    query << escapeString(getArrayAsString(record["carcinogenesis_and_mutagenesis_and_impairment_of_fertility"]));
    
    query << ")";
    
    if (mysql_query(mysql_conn, query.str().c_str())) {
        std::cerr << "Error inserting drug label record: " << mysql_error(mysql_conn) << std::endl;
        return false;
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
        std::cerr << "Error inserting record: " << mysql_error(mysql_conn) << std::endl;
        return false;
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

bool OpenFDADownloader::clearDatabase() {
    std::cout << "⏳ Clearing database tables..." << std::endl;
    
    // Helper function to check if table exists
    auto tableExists = [this](const std::string& tableName) -> bool {
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
    };
    
    // Clear NDC table if it exists
    if (tableExists("ndc_data")) {
        const char* clear_ndc_query = "DELETE FROM ndc_data";
        if (mysql_query(mysql_conn, clear_ndc_query)) {
            std::cerr << "❌ Error clearing NDC table: " << mysql_error(mysql_conn) << std::endl;
            return false;
        }
        
        const char* reset_ndc_ai = "ALTER TABLE ndc_data AUTO_INCREMENT = 1";
        if (mysql_query(mysql_conn, reset_ndc_ai)) {
            std::cerr << "❌ Error resetting NDC auto increment: " << mysql_error(mysql_conn) << std::endl;
            return false;
        }
        std::cout << "✅ NDC table cleared" << std::endl;
    } else {
        std::cout << "ℹ️  NDC table doesn't exist, skipping" << std::endl;
    }
    
    // Clear DrugsFDA table if it exists
    if (tableExists("drugsfda_data")) {
        const char* clear_drugsfda_query = "DELETE FROM drugsfda_data";
        if (mysql_query(mysql_conn, clear_drugsfda_query)) {
            std::cerr << "❌ Error clearing DrugsFDA table: " << mysql_error(mysql_conn) << std::endl;
            return false;
        }
        
        const char* reset_drugsfda_ai = "ALTER TABLE drugsfda_data AUTO_INCREMENT = 1";
        if (mysql_query(mysql_conn, reset_drugsfda_ai)) {
            std::cerr << "❌ Error resetting DrugsFDA auto increment: " << mysql_error(mysql_conn) << std::endl;
            return false;
        }
        std::cout << "✅ DrugsFDA table cleared" << std::endl;
    } else {
        std::cout << "ℹ️  DrugsFDA table doesn't exist, skipping" << std::endl;
    }
    
    // Clear Drug Label table if it exists
    if (tableExists("drug_label_data")) {
        const char* clear_drug_label_query = "DELETE FROM drug_label_data";
        if (mysql_query(mysql_conn, clear_drug_label_query)) {
            std::cerr << "❌ Error clearing Drug Label table: " << mysql_error(mysql_conn) << std::endl;
            return false;
        }
        
        const char* reset_drug_label_ai = "ALTER TABLE drug_label_data AUTO_INCREMENT = 1";
        if (mysql_query(mysql_conn, reset_drug_label_ai)) {
            std::cerr << "❌ Error resetting Drug Label auto increment: " << mysql_error(mysql_conn) << std::endl;
            return false;
        }
        std::cout << "✅ Drug Label table cleared" << std::endl;
    } else {
        std::cout << "ℹ️  Drug Label table doesn't exist, skipping" << std::endl;
    }
    
    std::cout << "✅ Database clearing completed" << std::endl;
    return true;
}

void OpenFDADownloader::processAllData() {
    if (data_source == DataSource::NDC_BULK) {
        std::cout << "🚀 Starting NDC bulk data processing..." << std::endl;
        
        std::string zip_filename = temp_dir + "/ndc-bulk.zip";
        std::string url = "https://download.open.fda.gov/drug/ndc/drug-ndc-0001-of-0001.json.zip";
        
        if (!downloadBulkFile(url, zip_filename)) {
            std::cerr << "❌ Failed to download bulk file" << std::endl;
            return;
        }
        
        if (!extractZipFile(zip_filename)) {
            std::cerr << "❌ Failed to extract ZIP file" << std::endl;
            return;
        }
        
        std::string json_filename = temp_dir + "/drug-ndc-0001-of-0001.json";
        if (!processBulkJsonFile(json_filename)) {
            std::cerr << "❌ Failed to process JSON file" << std::endl;
            return;
        }
        
        std::cout << "🎉 NDC bulk data processing completed successfully!" << std::endl;
        return;
    }
    
    if (data_source == DataSource::DRUG_LABEL_BULK) {
        std::cout << "🚀 Starting Drug Label bulk data processing..." << std::endl;
        
        // Download all 13 parts of drug label data
        const int total_parts = 13;
        int successful_parts = 0;
        
        for (int part = 1; part <= total_parts; part++) {
            std::cout << "📥 Downloading part " << part << " of " << total_parts << "..." << std::endl;
            
            std::stringstream url_stream;
            url_stream << "https://download.open.fda.gov/drug/label/drug-label-"
                      << std::setfill('0') << std::setw(4) << part 
                      << "-of-" << std::setfill('0') << std::setw(4) << total_parts 
                      << ".json.zip";
            
            std::stringstream filename_stream;
            filename_stream << temp_dir << "/drug-label-part-" << std::setfill('0') << std::setw(4) << part << ".zip";
            
            std::string url = url_stream.str();
            std::string zip_filename = filename_stream.str();
            
            if (!downloadBulkFile(url, zip_filename)) {
                std::cerr << "❌ Failed to download part " << part << std::endl;
                continue;
            }
            
            if (!extractZipFile(zip_filename)) {
                std::cerr << "❌ Failed to extract part " << part << std::endl;
                continue;
            }
            
            std::stringstream json_filename_stream;
            json_filename_stream << temp_dir << "/drug-label-"
                                << std::setfill('0') << std::setw(4) << part 
                                << "-of-" << std::setfill('0') << std::setw(4) << total_parts 
                                << ".json";
            
            std::string json_filename = json_filename_stream.str();
            if (!processBulkJsonFile(json_filename)) {
                std::cerr << "❌ Failed to process part " << part << std::endl;
                continue;
            }
            
            successful_parts++;
            int percent = (successful_parts * 100) / total_parts;
            std::cout << "✅ Part " << part << " completed (" << successful_parts 
                     << "/" << total_parts << " - " << percent << "%)" << std::endl;
        }
        
        if (successful_parts > 0) {
            std::cout << "🎉 Drug Label bulk data processing completed! " 
                     << "Successfully processed " << successful_parts << " out of " 
                     << total_parts << " parts." << std::endl;
        } else {
            std::cerr << "❌ No drug label parts were successfully processed!" << std::endl;
        }
        return;
    }
    
    if (data_source == DataSource::DRUGSFDA_BULK) {
        std::cout << "🚀 Starting DrugsFDA bulk data processing..." << std::endl;
        
        std::string zip_filename = temp_dir + "/drugsfda-bulk.zip";
        std::string url = "https://download.open.fda.gov/drug/drugsfda/drug-drugsfda-0001-of-0001.json.zip";
        
        if (!downloadBulkFile(url, zip_filename)) {
            std::cerr << "❌ Failed to download bulk file" << std::endl;
            return;
        }
        
        if (!extractZipFile(zip_filename)) {
            std::cerr << "❌ Failed to extract ZIP file" << std::endl;
            return;
        }
        
        std::string json_filename = temp_dir + "/drug-drugsfda-0001-of-0001.json";
        if (!processBulkJsonFile(json_filename)) {
            std::cerr << "❌ Failed to process JSON file" << std::endl;
            return;
        }
        
        std::cout << "🎉 Bulk data processing completed successfully!" << std::endl;
        return;
    }
    
    // Original NDC API processing
    const int batch_size = 1000;
    int skip = 0;
    int total_processed = 0;
    
    while (true) {
        std::cout << "Downloading batch starting at record " << skip << std::endl;
        
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
                
                std::cout << "Total records processed: " << total_processed << std::endl;
                
                if (batch_count < batch_size) {
                    std::cout << "Reached end of available data" << std::endl;
                    delete reader;
                    break;
                }
            }
        }
        delete reader;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Data download and processing completed. Total records: " << total_processed << std::endl;
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] [config_file]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -n, --ndc         Use NDC API (default)" << std::endl;
    std::cout << "  -b, --ndc-bulk    Use NDC bulk download" << std::endl;
    std::cout << "  -d, --drugsfda    Use DrugsFDA bulk download" << std::endl;
    std::cout << "  -l, --drug-label  Use Drug Label bulk download (13 parts)" << std::endl;
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
    std::cout << "  " << program_name << " --clear --drug-label # Clear database, then Drug Label bulk" << std::endl;
    std::cout << "  " << program_name << " -c mydb.conf      # Custom config file" << std::endl;
    std::cout << "  " << program_name << " -i                # Interactive TUI mode" << std::endl;
}

class TUIManager {
public:
    static void clearScreen() {
        std::cout << "\033[2J\033[H" << std::flush;
    }
    
    static void showBanner() {
        std::cout << "╔══════════════════════════════════════════════╗" << std::endl;
        std::cout << "║            🚀 OpenFDA Data Downloader        ║" << std::endl;
        std::cout << "║              Interactive Mode                ║" << std::endl;
        std::cout << "╚══════════════════════════════════════════════╝" << std::endl;
        std::cout << std::endl;
    }
    
    static int showMenu(const std::vector<std::string>& options, const std::string& title) {
        clearScreen();
        showBanner();
        
        std::cout << "📋 " << title << std::endl;
        std::cout << std::string(title.length() + 3, '─') << std::endl;
        std::cout << std::endl;
        
        for (size_t i = 0; i < options.size(); i++) {
            std::cout << "  " << (i + 1) << ". " << options[i] << std::endl;
        }
        std::cout << std::endl;
        
        int choice;
        while (true) {
            std::cout << "Enter your choice (1-" << options.size() << "): ";
            std::cin >> choice;
            
            if (std::cin.fail() || choice < 1 || choice > static_cast<int>(options.size())) {
                std::cin.clear();
                std::cin.ignore(10000, '\n');
                std::cout << "❌ Invalid choice. Please try again." << std::endl;
                continue;
            }
            
            std::cin.ignore(10000, '\n');
            return choice;
        }
    }
    
    static bool showYesNoDialog(const std::string& question) {
        std::cout << std::endl;
        std::cout << "❓ " << question << " (y/n): ";
        
        char response;
        while (true) {
            std::cin >> response;
            std::cin.ignore(10000, '\n');
            
            response = tolower(response);
            if (response == 'y') {
                return true;
            } else if (response == 'n') {
                return false;
            } else {
                std::cout << "❌ Please enter 'y' for yes or 'n' for no: ";
            }
        }
    }
    
    static std::string getStringInput(const std::string& prompt, const std::string& defaultValue = "") {
        std::cout << prompt;
        if (!defaultValue.empty()) {
            std::cout << " (default: " << defaultValue << ")";
        }
        std::cout << ": ";
        
        std::string input;
        std::getline(std::cin, input);
        
        if (input.empty() && !defaultValue.empty()) {
            return defaultValue;
        }
        
        return input;
    }
    
    static void showConfigSummary(DataSource dataSource, bool clearDb, const std::string& configFile) {
        std::cout << std::endl;
        std::cout << "📋 Configuration Summary" << std::endl;
        std::cout << "═══════════════════════" << std::endl;
        
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
        }
        
        std::cout << "  Data Source: " << sourceStr << std::endl;
        std::cout << "  Clear Database: " << (clearDb ? "Yes" : "No") << std::endl;
        std::cout << "  Config File: " << configFile << std::endl;
        std::cout << std::endl;
    }
    
    static void waitForEnter() {
        std::cout << "Press Enter to continue...";
        std::cin.get();
    }
};

bool runInteractiveMode(DataSource& dataSource, bool& clearDatabase, std::string& configFile) {
    TUIManager::clearScreen();
    TUIManager::showBanner();
    
    // Data source selection
    std::vector<std::string> dataSourceOptions = {
        "NDC API (Multiple API requests - slower but always up-to-date)",
        "NDC Bulk Download (Single large file - faster)",
        "DrugsFDA Bulk Download (Single large file - different dataset)",
        "Drug Label Bulk Download (13 large files - comprehensive label data)"
    };
    
    int choice = TUIManager::showMenu(dataSourceOptions, "Select Data Source");
    
    switch (choice) {
        case 1: dataSource = DataSource::NDC_API; break;
        case 2: dataSource = DataSource::NDC_BULK; break;
        case 3: dataSource = DataSource::DRUGSFDA_BULK; break;
        case 4: dataSource = DataSource::DRUG_LABEL_BULK; break;
    }
    
    // Clear database option
    clearDatabase = TUIManager::showYesNoDialog("Clear existing database tables before import?");
    
    // Config file option
    std::cout << std::endl;
    configFile = TUIManager::getStringInput("Database config file path", "database.nfo");
    
    // Show summary and confirm
    TUIManager::showConfigSummary(dataSource, clearDatabase, configFile);
    
    bool proceed = TUIManager::showYesNoDialog("Proceed with these settings?");
    
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
        {"clear",       no_argument,       0, 'x'},
        {"interactive", no_argument,       0, 'i'},
        {"config",      required_argument, 0, 'c'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((c = getopt_long(argc, argv, "nbdlxic:h", long_options, &option_index)) != -1) {
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