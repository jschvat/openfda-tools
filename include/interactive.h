#pragma once

#include "types.h"
#include "database.h"
#include "tui.h"
#include <string>

class InteractiveManager {
private:
    AdvancedTUI tui;
    DatabaseManager& db_manager;
    
public:
    InteractiveManager(DatabaseManager& db_manager);
    ~InteractiveManager();
    
    bool runInteractiveMode();
    bool runInteractiveMode(const std::string& config_file);
    
private:
    // Menu handlers
    bool handleDataDownload();
    bool handleDatabaseManagement();
    bool handleConfiguration();
    
    // Data source selection
    DataSource selectDataSource();
    
    // Database operations
    bool handleClearDatabase();
    bool handleDatabaseStatus();
    
    // Configuration management
    bool handleEditDatabaseConfig();
    bool handleSaveCurrentConfig();
    bool handleLoadConfigFile();
    
    // Enhanced database management
    bool handleTestConnection();
    bool handleCreateDatabaseAndTables();
    bool handleSwitchDatabaseType();
    
    // Database initialization
    void initializeDatabaseConnection();
    void initializeDatabaseConnection(const std::string& config_file);
};