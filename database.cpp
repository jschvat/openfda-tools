#include "database.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

DatabaseManager::DatabaseManager() : mysql_conn(nullptr), error_logger_ptr(nullptr) {}

DatabaseManager::~DatabaseManager() {
    if (mysql_conn) {
        mysql_close(mysql_conn);
    }
}

bool DatabaseManager::loadDatabaseConfig(const std::string& config_file) {
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

bool DatabaseManager::initializeDatabase() {
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
    
    // Set larger packet size to handle large text fields
    if (mysql_query(mysql_conn, "SET GLOBAL max_allowed_packet=1073741824")) {  // 1GB
        std::cout << "⚠️  Warning: Could not set max_allowed_packet (may need admin privileges)" << std::endl;
    }
    if (mysql_query(mysql_conn, "SET SESSION max_allowed_packet=1073741824")) {  // 1GB for this session
        std::cout << "⚠️  Warning: Could not set session max_allowed_packet" << std::endl;
    }
    
    std::cout << "✓ Connected to MySQL server" << std::endl;
    return true;
}

bool DatabaseManager::ensureSchemaExists() {
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

std::string DatabaseManager::escapeString(const std::string& input) {
    if (input.empty()) return "NULL";
    
    char* escaped = new char[input.length() * 2 + 1];
    mysql_real_escape_string(mysql_conn, escaped, input.c_str(), input.length());
    std::string result = "'" + std::string(escaped) + "'";
    delete[] escaped;
    return result;
}

bool DatabaseManager::isDuplicateKeyError(const std::string& error_msg) {
    return error_msg.find("Duplicate entry") != std::string::npos ||
           error_msg.find("1062") != std::string::npos ||
           error_msg.find("uk_product_ndc") != std::string::npos;
}

bool DatabaseManager::tableExists(const std::string& tableName) {
    std::string query = "SHOW TABLES LIKE '" + tableName + "'";
    if (mysql_query(mysql_conn, query.c_str())) {
        return false;
    }
    MYSQL_RES* result = mysql_store_result(mysql_conn);
    if (!result) {
        return false;
    }
    bool exists = mysql_num_rows(result) > 0;
    mysql_free_result(result);
    return exists;
}

bool DatabaseManager::clearTable(const std::string& tableName) {
    if (!tableExists(tableName)) {
        std::cout << "ℹ️  " << tableName << " table doesn't exist, skipping" << std::endl;
        return true;
    }
    
    std::string clear_query = "DELETE FROM " + tableName;
    if (mysql_query(mysql_conn, clear_query.c_str())) {
        std::cerr << "❌ Error clearing " << tableName << " table: " 
                  << mysql_error(mysql_conn) << std::endl;
        return false;
    }
    
    std::cout << "✅ " << tableName << " table cleared" << std::endl;
    return true;
}

bool DatabaseManager::clearDatabase() {
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
        std::cerr << "❌ Database clearing failed" << std::endl;
    }
    
    return success;
}

// Table Creation Methods

bool DatabaseManager::createNDCTable() {
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

bool DatabaseManager::createDrugsFDATable() {
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

bool DatabaseManager::createDrugLabelTable() {
    std::cout << "⏳ Creating Drug Label data table..." << std::endl;
    
    const char* create_table_query = R"(
        CREATE TABLE IF NOT EXISTS drug_label_data (
            id INT AUTO_INCREMENT PRIMARY KEY,
            set_id VARCHAR(100),
            effective_time DATE,
            version VARCHAR(500),
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

bool DatabaseManager::createFDANDCDataTable() {
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

// Helper Methods

std::string DatabaseManager::getArrayAsString(const Json::Value& array) {
    if (!array.isArray() || array.empty()) return "";
    std::stringstream ss;
    for (int i = 0; i < static_cast<int>(array.size()); i++) {
        if (i > 0) ss << ", ";
        ss << array[i].asString();
    }
    return ss.str();
}

std::string DatabaseManager::formatDate(const std::string& dateStr) {
    if (dateStr.empty() || dateStr.length() < 8) return "";
    
    std::string year = dateStr.substr(0, 4);
    std::string month = dateStr.substr(4, 2);
    std::string day = dateStr.substr(6, 2);
    
    // Validate basic date components
    int y = std::stoi(year);
    int m = std::stoi(month);
    int d = std::stoi(day);
    
    // Check for invalid dates
    if (y < 1900 || y > 2100) return "";  // Reasonable year range
    if (m < 1 || m > 12) return "";       // Invalid month
    if (d < 1 || d > 31) return "";       // Invalid day
    
    // Handle common invalid day cases
    if (d == 0) {
        d = 1;  // Convert day 00 to 01
        day = "01";
    }
    
    // Basic month-specific day validation
    if (m == 2 && d > 29) return "";  // February max 29 days
    if ((m == 4 || m == 6 || m == 9 || m == 11) && d > 30) return "";  // 30-day months
    
    return year + "-" + month + "-" + day;
}

std::string DatabaseManager::formatNDCToStandard(const std::string& ndc) {
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

std::string DatabaseManager::getFirstElement(const Json::Value& parent, const std::string& key) {
    if (!parent.isMember(key)) return "";
    
    const Json::Value& value = parent[key];
    if (value.isArray() && !value.empty()) {
        return value[0].asString();
    } else if (value.isString()) {
        return value.asString();
    }
    return "";
}