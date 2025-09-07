#include "include/database.h"
#include <iostream>

int main() {
    DatabaseManager db_manager;
    
    // Test loading the PostgreSQL configuration
    if (!db_manager.loadDatabaseConfig("database.nfo")) {
        std::cerr << "❌ Failed to load database configuration" << std::endl;
        return 1;
    }
    
    std::cout << "✅ Successfully loaded PostgreSQL configuration" << std::endl;
    std::cout << "Database type: " << (db_manager.getDatabaseType() == DatabaseType::POSTGRESQL ? "PostgreSQL" : "MySQL") << std::endl;
    
    // Test database connection
    std::cout << "🔗 Attempting to connect to PostgreSQL database..." << std::endl;
    
    if (!db_manager.initializeDatabase()) {
        std::cerr << "❌ Failed to connect to PostgreSQL database" << std::endl;
        std::cerr << "Connection details:" << std::endl;
        std::cerr << "  Host: miniserver.local" << std::endl;
        std::cerr << "  Port: 5432" << std::endl;
        std::cerr << "  User: jason" << std::endl;
        std::cerr << "  Database: openfda" << std::endl;
        return 1;
    }
    
    std::cout << "✅ Successfully connected to PostgreSQL database!" << std::endl;
    
    // Test schema creation
    std::cout << "🔧 Testing schema creation..." << std::endl;
    if (!db_manager.ensureSchemaExists()) {
        std::cerr << "❌ Failed to create/verify schema" << std::endl;
        return 1;
    }
    
    std::cout << "✅ Schema verification successful!" << std::endl;
    
    // Test table creation
    std::cout << "📊 Testing NDC table creation..." << std::endl;
    if (!db_manager.createNDCTable()) {
        std::cerr << "❌ Failed to create NDC table" << std::endl;
        return 1;
    }
    
    std::cout << "✅ NDC table creation successful!" << std::endl;
    std::cout << "🎉 PostgreSQL support is working correctly!" << std::endl;
    
    return 0;
}