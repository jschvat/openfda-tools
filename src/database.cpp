#include "database.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>

DatabaseManager::DatabaseManager() : db_conn(nullptr), is_multi_config(false), is_interactive_mode(false), error_logger_ptr(nullptr), tui_output_func(nullptr) {}

DatabaseManager::~DatabaseManager() {
    if (db_conn) {
        db_conn->disconnect();
    }
}

bool DatabaseManager::loadDatabaseConfig(const std::string& config_file) {
    std::vector<std::string> search_paths;
    
    // If config_file has a path (contains '/'), use it as-is first
    if (config_file.find('/') != std::string::npos) {
        search_paths.push_back(config_file);
    } else {
        // For relative filenames, search in multiple locations
        search_paths.push_back(config_file);           // Current directory
        search_paths.push_back("../" + config_file);   // Parent directory
    }
    
    std::ifstream file;
    std::string found_path;
    
    // Try each search path
    for (const auto& path : search_paths) {
        file.open(path);
        if (file.is_open()) {
            found_path = path;
            outputMessage("✓ Found database config: " + found_path);
            break;
        }
        file.clear(); // Clear any error flags
    }
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open config file. Searched locations:" << std::endl;
        for (const auto& path : search_paths) {
            std::cerr << "  - " << path << std::endl;
        }
        return false;
    }
    
    std::string line;
    bool has_sections = false;
    
    // First pass: check if this is a multi-database config (has [sections])
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] == '[' && line.back() == ']') {
            has_sections = true;
            break;
        }
    }
    
    // If it has sections, use multi-database parser
    if (has_sections) {
        file.close();
        return loadMultiDatabaseConfig(found_path);
    }
    
    // Reset file for single-database parsing
    file.clear();
    file.seekg(0);
    
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
        
        if (key == "type" || key == "database_type") {
            if (value == "mysql" || value == "MYSQL") {
                db_config.type = DatabaseType::MYSQL;
            } else if (value == "postgresql" || value == "postgres" || value == "POSTGRESQL" || value == "POSTGRES") {
                db_config.type = DatabaseType::POSTGRESQL;
            }
        } else if (key == "host") {
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
        db_config.port = (db_config.type == DatabaseType::POSTGRESQL) ? 5432 : 3306;
    }
    
    outputMessage("✓ Loaded database configuration:");
    outputMessage("  Type: " + std::string(db_config.type == DatabaseType::POSTGRESQL ? "PostgreSQL" : "MySQL"));
    outputMessage("  Host: " + db_config.host + ":" + std::to_string(db_config.port));
    outputMessage("  Database: " + db_config.database);
    outputMessage("  Schema: " + db_config.schema);
    
    return true;
}

bool DatabaseManager::loadMultiDatabaseConfig(const std::string& config_file) {
    std::vector<std::string> search_paths;
    
    // If config_file has a path (contains '/'), use it as-is first
    if (config_file.find('/') != std::string::npos) {
        search_paths.push_back(config_file);
    } else {
        // For relative filenames, search in multiple locations
        search_paths.push_back(config_file);           // Current directory
        search_paths.push_back("../" + config_file);   // Parent directory
    }
    
    std::ifstream file;
    std::string found_path;
    
    // Try each search path
    for (const auto& path : search_paths) {
        file.open(path);
        if (file.is_open()) {
            found_path = path;
            if (!is_interactive_mode) {
                std::cout << "✓ Found multi-database config: " << found_path << std::endl;
            }
            break;
        }
        file.clear(); // Clear any error flags
    }
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open multi-database config file. Searched locations:" << std::endl;
        for (const auto& path : search_paths) {
            std::cerr << "  - " << path << std::endl;
        }
        return false;
    }
    
    std::string line;
    std::string current_section = "";
    DatabaseConfig* current_config = nullptr;
    
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        // Check for section headers [mysql] or [postgresql]
        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.length() - 2);
            if (current_section == "mysql") {
                current_config = &multi_config.mysql_config;
            } else if (current_section == "postgresql") {
                current_config = &multi_config.postgresql_config;
            }
            continue;
        }
        
        size_t pos = line.find(':');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Handle global settings (not in a section)
        if (current_section.empty()) {
            if (key == "active_database") {
                multi_config.active_database = value;
            }
            continue;
        }
        
        // Handle database-specific settings
        if (current_config) {
            if (key == "host") {
                current_config->host = value;
            } else if (key == "port") {
                current_config->port = std::stoi(value);
            } else if (key == "user") {
                current_config->user = value;
            } else if (key == "password") {
                current_config->password = value;
            } else if (key == "database") {
                current_config->database = value;
            } else if (key == "schema") {
                current_config->schema = value;
            }
        }
    }
    
    file.close();
    
    // Set default ports if not specified
    if (multi_config.mysql_config.port == 0) {
        multi_config.mysql_config.port = 3306;
    }
    if (multi_config.postgresql_config.port == 0) {
        multi_config.postgresql_config.port = 5432;
    }
    
    // Set the active configuration
    db_config = multi_config.getActiveConfig();
    is_multi_config = true;
    
    if (!is_interactive_mode) {
        std::cout << "✓ Loaded multi-database configuration:" << std::endl;
        std::cout << "  Active: " << multi_config.active_database << std::endl;
        std::cout << "  Type: " << (db_config.type == DatabaseType::POSTGRESQL ? "PostgreSQL" : "MySQL") << std::endl;
        std::cout << "  Host: " << db_config.host << ":" << db_config.port << std::endl;
        std::cout << "  Database: " << db_config.database << std::endl;
        std::cout << "  Schema: " << db_config.schema << std::endl;
    }
    
    return true;
}

bool DatabaseManager::switchDatabase(const std::string& database_name) {
    if (!is_multi_config) {
        std::cerr << "Error: Not using multi-database configuration" << std::endl;
        return false;
    }
    
    if (database_name != "mysql" && database_name != "postgresql") {
        std::cerr << "Error: Invalid database name. Use 'mysql' or 'postgresql'" << std::endl;
        return false;
    }
    
    // Disconnect current connection
    if (db_conn && db_conn->isConnected()) {
        db_conn->disconnect();
        db_conn.reset();
    }
    
    // Switch to new configuration
    db_config = multi_config.selectDatabase(database_name);
    
    if (!is_interactive_mode) {
        std::cout << "✓ Switched to " << database_name << " database" << std::endl;
    }
    std::cout << "  Type: " << (db_config.type == DatabaseType::POSTGRESQL ? "PostgreSQL" : "MySQL") << std::endl;
    std::cout << "  Host: " << db_config.host << ":" << db_config.port << std::endl;
    std::cout << "  Database: " << db_config.database << std::endl;
    
    return true;
}

bool DatabaseManager::initializeDatabase() {
    std::string db_type_name = (db_config.type == DatabaseType::POSTGRESQL) ? "PostgreSQL" : "MySQL";
    if (!is_interactive_mode) {
        std::cout << "⏳ Initializing " << db_type_name << " connection..." << std::endl;
    }
    
    // Create appropriate database connection
    db_conn = createDatabaseConnection(db_config.type);
    if (!db_conn) {
        std::cerr << "❌ Error: Failed to create " << db_type_name << " connection" << std::endl;
        return false;
    }
    
    // Connect to database
    if (!db_conn->connect(db_config)) {
        std::cerr << "❌ Error: Failed to connect to " << db_type_name << " server: " 
                  << db_conn->getLastError() << std::endl;
        return false;
    }
    
    // Set database-specific configurations
    if (db_config.type == DatabaseType::MYSQL) {
        // Set larger packet size to handle large text fields for MySQL
        if (!db_conn->executeQuery("SET GLOBAL max_allowed_packet=1073741824")) {  // 1GB
            std::cout << "⚠️  Warning: Could not set max_allowed_packet (may need admin privileges)" << std::endl;
        }
        if (!db_conn->executeQuery("SET SESSION max_allowed_packet=1073741824")) {  // 1GB for this session
            std::cout << "⚠️  Warning: Could not set session max_allowed_packet" << std::endl;
        }
    }
    
    if (!is_interactive_mode) {
        std::cout << "✓ Connected to " << db_type_name << " server" << std::endl;
    }
    return true;
}

bool DatabaseManager::ensureSchemaExists() {
    if (!is_interactive_mode) {
        std::cout << "⏳ Checking database and schema..." << std::endl;
    }
    
    if (db_config.type == DatabaseType::MYSQL) {
        // For MySQL, create database if it doesn't exist
        std::string create_db_query = "CREATE DATABASE IF NOT EXISTS `" + db_config.database + 
                                      "` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci";
        
        if (!db_conn->executeQuery(create_db_query)) {
            std::cerr << "❌ Error creating database: " << db_conn->getLastError() << std::endl;
            return false;
        }
        
        // Use the database
        std::string use_db_query = "USE `" + db_config.database + "`";
        if (!db_conn->executeQuery(use_db_query)) {
            std::cerr << "❌ Error selecting database: " << db_conn->getLastError() << std::endl;
            return false;
        }
        
    } else if (db_config.type == DatabaseType::POSTGRESQL) {
        // For PostgreSQL, we need to handle database creation differently
        // First disconnect and connect to postgres system database
        bool need_to_create_db = false;
        std::string original_database = db_config.database;
        
        // Try connecting to the target database first
        if (!db_conn->isConnected()) {
            std::cerr << "❌ Error: Not connected to PostgreSQL server" << std::endl;
            return false;
        }
        
        // Check if database exists by trying to connect to it
        std::string check_db_query = "SELECT 1 FROM pg_database WHERE datname = '" + db_config.database + "'";
        std::vector<std::vector<std::string>> result;
        if (!db_conn->executeQuery(check_db_query, result)) {
            std::cerr << "❌ Error checking database existence: " << db_conn->getLastError() << std::endl;
            return false;
        }
        
        // If database doesn't exist, create it
        if (result.empty()) {
            std::cout << "🔧 Creating PostgreSQL database: " << db_config.database << std::endl;
            std::string create_db_query = "CREATE DATABASE \"" + db_config.database + 
                                         "\" WITH ENCODING='UTF8' LC_COLLATE='en_US.UTF-8' LC_CTYPE='en_US.UTF-8'";
            
            if (!db_conn->executeQuery(create_db_query)) {
                std::cerr << "❌ Error creating database: " << db_conn->getLastError() << std::endl;
                return false;
            }
            if (!is_interactive_mode) {
                std::cout << "✓ Database '" << db_config.database << "' created successfully" << std::endl;
            }
        }
        
        // Now create schema if needed and it's different from database name
        if (db_config.schema != "public" && !db_config.schema.empty()) {
            std::string create_schema_query = "CREATE SCHEMA IF NOT EXISTS \"" + db_config.schema + "\"";
            if (!db_conn->executeQuery(create_schema_query)) {
                std::cerr << "❌ Error creating schema: " << db_conn->getLastError() << std::endl;
                return false;
            }
            
            // Set search path to include our schema
            std::string search_path_query = "SET search_path TO \"" + db_config.schema + "\", public";
            if (!db_conn->executeQuery(search_path_query)) {
                std::cerr << "❌ Error setting search path: " << db_conn->getLastError() << std::endl;
                return false;
            }
            if (!is_interactive_mode) {
                std::cout << "✓ Schema '" << db_config.schema << "' ready" << std::endl;
            }
        }
    }
    
    if (!is_interactive_mode) {
        std::cout << "✓ Database '" << db_config.database << "' ready" << std::endl;
    }
    return true;
}

std::string DatabaseManager::escapeString(const std::string& input) {
    if (input.empty()) return "NULL";
    
    if (!db_conn) return "'" + input + "'";
    
    return "'" + db_conn->escapeString(input) + "'";
}

bool DatabaseManager::isDuplicateKeyError(const std::string& error_msg) {
    if (db_config.type == DatabaseType::MYSQL) {
        return error_msg.find("Duplicate entry") != std::string::npos ||
               error_msg.find("1062") != std::string::npos ||
               error_msg.find("uk_product_ndc") != std::string::npos;
    } else if (db_config.type == DatabaseType::POSTGRESQL) {
        return error_msg.find("duplicate key") != std::string::npos ||
               error_msg.find("23505") != std::string::npos ||
               error_msg.find("unique constraint") != std::string::npos;
    }
    return false;
}

bool DatabaseManager::tableExists(const std::string& tableName) {
    std::string query;
    
    if (db_config.type == DatabaseType::MYSQL) {
        query = "SHOW TABLES LIKE '" + tableName + "'";
    } else if (db_config.type == DatabaseType::POSTGRESQL) {
        query = "SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_name = '" + tableName + "')";
        if (!db_config.schema.empty() && db_config.schema != db_config.database) {
            query = "SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_schema = '" + 
                   db_config.schema + "' AND table_name = '" + tableName + "')";
        }
    }
    
    std::vector<std::vector<std::string>> results;
    if (!db_conn->executeQuery(query, results)) {
        return false;
    }
    
    if (db_config.type == DatabaseType::MYSQL) {
        return !results.empty();
    } else if (db_config.type == DatabaseType::POSTGRESQL) {
        return !results.empty() && results[0].size() > 0 && results[0][0] == "t";
    }
    
    return false;
}

bool DatabaseManager::clearTable(const std::string& tableName) {
    if (!tableExists(tableName)) {
        std::cout << "ℹ️  " << tableName << " table doesn't exist, skipping" << std::endl;
        return true;
    }
    
    std::string clear_query = "DELETE FROM " + tableName;
    if (!db_conn->executeQuery(clear_query)) {
        std::cerr << "❌ Error clearing " << tableName << " table: " 
                  << db_conn->getLastError() << std::endl;
        return false;
    }
    
    std::cout << "✅ " << tableName << " table cleared" << std::endl;
    return true;
}

bool DatabaseManager::clearDatabase() {
    if (!is_interactive_mode) {
        std::cout << "⏳ Clearing database tables..." << std::endl;
    }
    
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
    if (!is_interactive_mode) {
        std::cout << "⏳ Creating NDC data table..." << std::endl;
    }
    
    std::string create_table_query = getNDCTableSchema();
    if (create_table_query.empty()) {
        std::cerr << "❌ Error: No schema defined for current database type" << std::endl;
        return false;
    }
    
    if (!db_conn->executeQuery(create_table_query)) {
        std::cerr << "❌ Error creating NDC table: " << db_conn->getLastError() << std::endl;
        return false;
    }
    
    if (!is_interactive_mode) {
        std::cout << "✓ NDC table ready" << std::endl;
    }
    return true;
}

bool DatabaseManager::createDrugsFDATable() {
    if (!is_interactive_mode) {
        std::cout << "⏳ Creating DrugsFDA data table..." << std::endl;
    }
    
    std::string create_table_query = getDrugsFDATableSchema();
    if (create_table_query.empty()) {
        std::cerr << "❌ Error: No schema defined for current database type" << std::endl;
        return false;
    }
    
    if (!db_conn->executeQuery(create_table_query)) {
        std::cerr << "❌ Error creating DrugsFDA table: " << db_conn->getLastError() << std::endl;
        return false;
    }
    
    if (!is_interactive_mode) {
        std::cout << "✓ DrugsFDA table ready" << std::endl;
    }
    return true;
}

bool DatabaseManager::createDrugLabelTable() {
    if (!is_interactive_mode) {
        std::cout << "⏳ Creating Drug Label data table..." << std::endl;
    }
    
    std::string create_table_query = getDrugLabelTableSchema();
    if (create_table_query.empty()) {
        std::cerr << "❌ Error: No schema defined for current database type" << std::endl;
        return false;
    }
    
    if (!db_conn->executeQuery(create_table_query)) {
        std::cerr << "❌ Error creating Drug Label table: " << db_conn->getLastError() << std::endl;
        return false;
    }
    
    if (!is_interactive_mode) {
        std::cout << "✓ Drug Label table ready" << std::endl;
    }
    return true;
}

bool DatabaseManager::createFDANDCDataTable() {
    if (!is_interactive_mode) {
        std::cout << "⏳ Creating FDA NDC data table..." << std::endl;
    }
    
    // Check if table exists and recreate it with new schema
    if (tableExists("fda_ndc_data")) {
        outputMessage("⚠️  FDA NDC table exists, recreating with new schema...");
        
        std::string drop_query = "DROP TABLE fda_ndc_data";
        if (!db_conn->executeQuery(drop_query)) {
            std::cerr << "❌ Error dropping old FDA NDC table: " << db_conn->getLastError() << std::endl;
            return false;
        }
    }
    
    std::string create_table_query = getFDANDCDataTableSchema();
    if (create_table_query.empty()) {
        std::cerr << "❌ Error: No schema defined for current database type" << std::endl;
        return false;
    }
    
    if (!db_conn->executeQuery(create_table_query)) {
        std::cerr << "❌ Error creating FDA NDC Data table: " << db_conn->getLastError() << std::endl;
        return false;
    }
    
    if (!is_interactive_mode) {
        std::cout << "✓ FDA NDC Data table ready" << std::endl;
    }
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

// Database-specific schema creation methods
std::string DatabaseManager::getNDCTableSchema() {
    if (db_config.type == DatabaseType::MYSQL) {
        return R"(
            CREATE TABLE IF NOT EXISTS ndc_data (
                id INT AUTO_INCREMENT PRIMARY KEY,
                product_ndc VARCHAR(255),
                generic_name TEXT,
                brand_name TEXT,
                labeler_name TEXT,
                product_type VARCHAR(255),
                dosage_form VARCHAR(255),
                route VARCHAR(255),
                marketing_start_date DATE,
                marketing_end_date DATE,
                product_id VARCHAR(255),
                application_number VARCHAR(255),
                brand_name_base TEXT,
                brand_name_suffix TEXT,
                active_ingredients JSON,
                packaging JSON,
                openfda_data JSON,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uk_product_ndc (product_ndc),
                INDEX idx_generic_name (generic_name(100)),
                INDEX idx_brand_name (brand_name(100)),
                INDEX idx_labeler_name (labeler_name(100))
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )";
    } else if (db_config.type == DatabaseType::POSTGRESQL) {
        return R"(
            CREATE TABLE IF NOT EXISTS ndc_data (
                id SERIAL PRIMARY KEY,
                product_ndc VARCHAR(255),
                generic_name TEXT,
                brand_name TEXT,
                labeler_name TEXT,
                product_type VARCHAR(255),
                dosage_form VARCHAR(255),
                route VARCHAR(255),
                marketing_start_date DATE,
                marketing_end_date DATE,
                product_id VARCHAR(255),
                application_number VARCHAR(255),
                brand_name_base TEXT,
                brand_name_suffix TEXT,
                active_ingredients JSONB,
                packaging JSONB,
                openfda_data JSONB,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                CONSTRAINT uk_product_ndc UNIQUE (product_ndc)
            );
            CREATE INDEX IF NOT EXISTS idx_ndc_generic_name ON ndc_data (generic_name);
            CREATE INDEX IF NOT EXISTS idx_ndc_brand_name ON ndc_data (brand_name);
            CREATE INDEX IF NOT EXISTS idx_ndc_labeler_name ON ndc_data (labeler_name);
        )";
    }
    return "";
}

std::string DatabaseManager::getDrugsFDATableSchema() {
    if (db_config.type == DatabaseType::MYSQL) {
        return R"(
            CREATE TABLE IF NOT EXISTS drugsfda_data (
                id INT AUTO_INCREMENT PRIMARY KEY,
                application_number VARCHAR(255),
                sponsor_name TEXT,
                submissions JSON,
                products JSON,
                openfda_data JSON,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uk_application_number (application_number),
                INDEX idx_sponsor_name (sponsor_name(100))
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )";
    } else if (db_config.type == DatabaseType::POSTGRESQL) {
        return R"(
            CREATE TABLE IF NOT EXISTS drugsfda_data (
                id SERIAL PRIMARY KEY,
                application_number VARCHAR(255),
                sponsor_name TEXT,
                submissions JSONB,
                products JSONB,
                openfda_data JSONB,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                CONSTRAINT uk_application_number UNIQUE (application_number)
            );
            CREATE INDEX IF NOT EXISTS idx_drugsfda_sponsor_name ON drugsfda_data (sponsor_name);
        )";
    }
    return "";
}

std::string DatabaseManager::getDrugLabelTableSchema() {
    if (db_config.type == DatabaseType::MYSQL) {
        return R"(
            CREATE TABLE IF NOT EXISTS drug_label_data (
                id INT AUTO_INCREMENT PRIMARY KEY,
                set_id VARCHAR(255),
                id_value VARCHAR(255),
                effective_time VARCHAR(50),
                version VARCHAR(50),
                spl_medguide JSON,
                spl_patient_package_insert JSON,
                purpose TEXT,
                indications_and_usage LONGTEXT,
                contraindications TEXT,
                warnings_and_cautions TEXT,
                adverse_reactions TEXT,
                drug_interactions TEXT,
                overdosage TEXT,
                dosage_and_administration TEXT,
                description TEXT,
                clinical_pharmacology TEXT,
                nonclinical_toxicology TEXT,
                clinical_studies TEXT,
                how_supplied TEXT,
                package_label_principal_display_panel LONGTEXT,
                openfda_data JSON,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uk_set_id (set_id),
                INDEX idx_effective_time (effective_time)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )";
    } else if (db_config.type == DatabaseType::POSTGRESQL) {
        return R"(
            CREATE TABLE IF NOT EXISTS drug_label_data (
                id SERIAL PRIMARY KEY,
                set_id VARCHAR(255),
                id_value VARCHAR(255),
                effective_time VARCHAR(50),
                version VARCHAR(50),
                spl_medguide JSONB,
                spl_patient_package_insert JSONB,
                purpose TEXT,
                indications_and_usage TEXT,
                contraindications TEXT,
                warnings_and_cautions TEXT,
                adverse_reactions TEXT,
                drug_interactions TEXT,
                overdosage TEXT,
                dosage_and_administration TEXT,
                description TEXT,
                clinical_pharmacology TEXT,
                nonclinical_toxicology TEXT,
                clinical_studies TEXT,
                how_supplied TEXT,
                package_label_principal_display_panel TEXT,
                openfda_data JSONB,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                CONSTRAINT uk_set_id UNIQUE (set_id)
            );
            CREATE INDEX IF NOT EXISTS idx_label_effective_time ON drug_label_data (effective_time);
        )";
    }
    return "";
}

std::string DatabaseManager::getFDANDCDataTableSchema() {
    if (db_config.type == DatabaseType::MYSQL) {
        return R"(
            CREATE TABLE IF NOT EXISTS fda_ndc_data (
                id INT AUTO_INCREMENT PRIMARY KEY,
                product_ndc VARCHAR(255),
                manufacturer_name TEXT,
                unii TEXT,
                product_type VARCHAR(255),
                spl_set_id VARCHAR(255),
                route TEXT,
                generic_name TEXT,
                brand_name TEXT,
                substance_name TEXT,
                spl_id VARCHAR(255),
                package_ndc VARCHAR(255),
                application_number VARCHAR(255),
                rxcui VARCHAR(255),
                pharm_class_moa TEXT,
                pharm_class_epc TEXT,
                pharm_class_cs TEXT,
                nui VARCHAR(255),
                pharm_class_pe TEXT,
                dosage_form VARCHAR(255),
                is_original_packager BOOLEAN,
                original_packager_product_ndc VARCHAR(255),
                upc VARCHAR(255),
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uk_fda_ndc_product_ndc (product_ndc),
                INDEX idx_fda_ndc_manufacturer_name (manufacturer_name(100)),
                INDEX idx_fda_ndc_brand_name (brand_name(100))
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
        )";
    } else if (db_config.type == DatabaseType::POSTGRESQL) {
        return R"(
            CREATE TABLE IF NOT EXISTS fda_ndc_data (
                id SERIAL PRIMARY KEY,
                product_ndc VARCHAR(255),
                manufacturer_name TEXT,
                unii TEXT,
                product_type VARCHAR(255),
                spl_set_id VARCHAR(255),
                route TEXT,
                generic_name TEXT,
                brand_name TEXT,
                substance_name TEXT,
                spl_id VARCHAR(255),
                package_ndc VARCHAR(255),
                application_number VARCHAR(255),
                rxcui VARCHAR(255),
                pharm_class_moa TEXT,
                pharm_class_epc TEXT,
                pharm_class_cs TEXT,
                nui VARCHAR(255),
                pharm_class_pe TEXT,
                dosage_form VARCHAR(255),
                is_original_packager BOOLEAN,
                original_packager_product_ndc VARCHAR(255),
                upc VARCHAR(255),
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                CONSTRAINT uk_fda_ndc_product_ndc UNIQUE (product_ndc)
            );
            CREATE INDEX IF NOT EXISTS idx_fda_ndc_manufacturer_name ON fda_ndc_data (manufacturer_name);
            CREATE INDEX IF NOT EXISTS idx_fda_ndc_brand_name ON fda_ndc_data (brand_name);
        )";
    }
    return "";
}

void DatabaseManager::outputMessage(const std::string& message) {
    if (is_interactive_mode && tui_output_func) {
        // Send to TUI footer
        tui_output_func(message);
    } else {
        // Send to console
        std::cout << message << std::endl;
    }
}