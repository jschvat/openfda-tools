#pragma once

#include <string>
#include <map>

enum class DataSource {
    NDC_API,
    NDC_BULK,
    DRUGSFDA_BULK,
    DRUG_LABEL_BULK,
    FDA_NDC_BULK
};

enum class DatabaseType {
    MYSQL,
    POSTGRESQL
};

struct DatabaseConfig {
    DatabaseType type;
    std::string host;
    int port;
    std::string user;
    std::string password;
    std::string database;
    std::string schema;
    
    DatabaseConfig() : type(DatabaseType::MYSQL), port(3306) {}
};

// Multi-database configuration support
struct MultiDatabaseConfig {
    std::string active_database; // "mysql" or "postgresql"
    DatabaseConfig mysql_config;
    DatabaseConfig postgresql_config;
    
    // Get the currently active configuration
    DatabaseConfig getActiveConfig() const {
        if (active_database == "postgresql") {
            return postgresql_config;
        }
        return mysql_config; // Default to MySQL if not specified
    }
    
    // Set active database and return the config
    DatabaseConfig selectDatabase(const std::string& db_name) {
        active_database = db_name;
        return getActiveConfig();
    }
    
    MultiDatabaseConfig() : active_database("mysql") {
        // Set default ports
        mysql_config.type = DatabaseType::MYSQL;
        mysql_config.port = 3306;
        postgresql_config.type = DatabaseType::POSTGRESQL; 
        postgresql_config.port = 5432;
    }
};

struct DataSourceConfig {
    std::string name;
    std::string base_url;
    std::string file_pattern;
    int file_count;
    bool is_multi_file;
    
    DataSourceConfig() : file_count(1), is_multi_file(false) {}
    
    DataSourceConfig(const std::string& n, const std::string& url, const std::string& pattern)
        : name(n), base_url(url), file_pattern(pattern), file_count(1), is_multi_file(false) {}
    
    DataSourceConfig(const std::string& n, const std::string& url, const std::string& pattern, int count)
        : name(n), base_url(url), file_pattern(pattern), file_count(count), is_multi_file(true) {}
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