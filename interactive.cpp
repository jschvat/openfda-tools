#include "interactive.h"
#include "data_processor.h"
#include <iostream>

InteractiveManager::InteractiveManager(DatabaseManager& db_manager) : db_manager(db_manager) {}

InteractiveManager::~InteractiveManager() {}

bool InteractiveManager::runInteractiveMode() {
    if (!tui.initialize()) {
        std::cerr << "❌ Failed to initialize TUI. Falling back to simple mode." << std::endl;
        return false;
    }
    
    tui.updateStatus("Interactive mode - Use arrow keys to navigate menus");
    
    while (true) {
        int choice = tui.showMainMenu();
        
        switch (choice) {
            case 1: // Download FDA Data
                if (!handleDataDownload()) {
                    tui.showError("Data download operation failed or was cancelled");
                }
                break;
                
            case 2: // Database Management
                if (!handleDatabaseManagement()) {
                    tui.showError("Database management operation failed or was cancelled");
                }
                break;
                
            case 3: // Configuration
                if (!handleConfiguration()) {
                    tui.showError("Configuration operation failed or was cancelled");
                }
                break;
                
            case 4: // Exit
            case -1: // User pressed 'q' or Escape
                tui.showMessage("Thank you for using OpenFDA Data Downloader!", 2000);
                return true;
                
            default:
                tui.showError("Invalid selection. Please try again.");
                break;
        }
    }
}

bool InteractiveManager::handleDataDownload() {
    DataSource dataSource = selectDataSource();
    if (dataSource == static_cast<DataSource>(-1)) {
        return false; // User cancelled
    }
    
    // Ask about clearing database
    bool clearDatabase = tui.showYesNoDialog("Clear existing database tables before import?");
    
    // Show summary and confirm
    std::string sourceStr;
    switch (dataSource) {
        case DataSource::NDC_API:
            sourceStr = "NDC API (Multiple requests - slower but always current)";
            break;
        case DataSource::NDC_BULK:
            sourceStr = "NDC Bulk Download (Single file - faster)";
            break;
        case DataSource::DRUGSFDA_BULK:
            sourceStr = "DrugsFDA Bulk Download (Different dataset)";
            break;
        case DataSource::DRUG_LABEL_BULK:
            sourceStr = "Drug Label Bulk Download (13 files - comprehensive)";
            break;
        case DataSource::FDA_NDC_BULK:
            sourceStr = "FDA NDC Bulk Download (Normalized format)";
            break;
    }
    
    tui.showOperationSummary("Data Download Operation", 
        "Source: " + sourceStr + "\nClear DB: " + (clearDatabase ? "Yes" : "No"));
    
    if (!tui.showYesNoDialog("Proceed with this data download?")) {
        return false;
    }
    
    // Set data source and clear database if requested
    db_manager.setDataSource(dataSource);
    
    if (clearDatabase) {
        tui.updateStatus("Clearing database tables...");
        if (!db_manager.clearDatabase()) {
            tui.showError("Failed to clear database tables");
            return false;
        }
        tui.showMessage("Database cleared successfully", 1500);
    }
    
    // Create appropriate table
    tui.updateStatus("Creating database tables...");
    bool table_created = false;
    
    if (dataSource == DataSource::NDC_API || dataSource == DataSource::NDC_BULK) {
        table_created = db_manager.createNDCTable();
    } else if (dataSource == DataSource::DRUGSFDA_BULK) {
        table_created = db_manager.createDrugsFDATable();
    } else if (dataSource == DataSource::DRUG_LABEL_BULK) {
        table_created = db_manager.createDrugLabelTable();
    } else if (dataSource == DataSource::FDA_NDC_BULK) {
        table_created = db_manager.createFDANDCDataTable();
    }
    
    if (!table_created) {
        tui.showError("Failed to create database table");
        return false;
    }
    
    // Process data
    tui.updateStatus("Processing data - this may take a while...");
    tui.showMessage("Data processing started. Check console for detailed progress.", 3000);
    
    // Switch to console mode for data processing
    tui.cleanup();
    
    DataProcessor processor(db_manager, "./logs/");
    processor.setDataSource(dataSource);
    processor.processAllData();
    
    // Reinitialize TUI
    if (!tui.initialize()) {
        std::cout << "Data processing completed. Returning to command line mode." << std::endl;
        return true;
    }
    
    tui.showMessage("Data processing completed successfully!", 3000);
    return true;
}

bool InteractiveManager::handleDatabaseManagement() {
    std::vector<std::string> dbOptions = {
        "Clear Tables - Remove data from tables",
        "View Status - Show connection and table info",
        "Test Connection - Verify MySQL connectivity",
        "Return to Main Menu"
    };
    
    int choice = tui.showMenu(dbOptions, "Database Management Options", 90);
    
    switch (choice) {
        case 1: // Clear Database
            return handleClearDatabase();
            
        case 2: // Database Status
            return handleDatabaseStatus();
            
        case 3: // Test Connection
            tui.updateStatus("Testing database connection...");
            if (db_manager.initializeDatabase()) {
                tui.showMessage("Database connection successful!", 2000);
                return true;
            } else {
                tui.showError("Database connection failed!");
                return false;
            }
            
        case 4: // Return to main menu
        case -1: // User cancelled
            return true;
            
        default:
            tui.showError("Invalid selection");
            return false;
    }
}

bool InteractiveManager::handleConfiguration() {
    std::vector<std::string> configOptions = {
        "Edit Database Config - Modify connection settings",
        "Save Config - Save settings to file",
        "Load Config File - Load existing settings",
        "Return to Main Menu"
    };
    
    int choice = tui.showMenu(configOptions, "Configuration Management", 90);
    
    switch (choice) {
        case 1: // Edit Database Config
            return handleEditDatabaseConfig();
            
        case 2: // Save Current Config
            return handleSaveCurrentConfig();
            
        case 3: // Load Config File
            return handleLoadConfigFile();
            
        case 4: // Return to main menu
        case -1: // User cancelled
            return true;
            
        default:
            tui.showError("Invalid selection");
            return false;
    }
}

DataSource InteractiveManager::selectDataSource() {
    std::vector<std::string> dataSourceOptions = {
        "NDC API - Multiple API requests (slower, current)",
        "NDC Bulk - Single file (faster)",
        "DrugsFDA Bulk - Single file (approval data)",
        "Drug Label Bulk - 13 files (comprehensive labels)",
        "FDA NDC Bulk - Single file (normalized NDC)"
    };
    
    int choice = tui.showMenu(dataSourceOptions, "Select Data Source for Download", 100);
    
    switch (choice) {
        case 1: return DataSource::NDC_API;
        case 2: return DataSource::NDC_BULK;
        case 3: return DataSource::DRUGSFDA_BULK;
        case 4: return DataSource::DRUG_LABEL_BULK;
        case 5: return DataSource::FDA_NDC_BULK;
        default: return static_cast<DataSource>(-1); // Cancelled
    }
}

bool InteractiveManager::handleClearDatabase() {
    std::vector<std::string> clearOptions = {
        "Clear NDC Table - Remove NDC records",
        "Clear DrugsFDA Table - Remove DrugsFDA records",
        "Clear Drug Label Table - Remove label records", 
        "Clear FDA NDC Table - Remove FDA NDC records",
        "Clear All Tables - Remove all data",
        "Cancel - Return without clearing"
    };
    
    int choice = tui.showMenu(clearOptions, "Select Tables to Clear", 90);
    
    if (choice == -1 || choice == 6) {
        return true; // User cancelled
    }
    
    if (!tui.showYesNoDialog("Are you sure you want to clear the selected table(s)?\nThis action cannot be undone!")) {
        return true;
    }
    
    tui.updateStatus("Clearing database tables...");
    
    bool success = false;
    switch (choice) {
        case 1:
            success = db_manager.clearTable("ndc_data");
            break;
        case 2:
            success = db_manager.clearTable("drugsfda_data");
            break;
        case 3:
            success = db_manager.clearTable("drug_label_data");
            break;
        case 4:
            success = db_manager.clearTable("fda_ndc_data");
            break;
        case 5:
            success = db_manager.clearTable("ndc_data") &&
                     db_manager.clearTable("drugsfda_data") &&
                     db_manager.clearTable("drug_label_data") &&
                     db_manager.clearTable("fda_ndc_data");
            break;
    }
    
    if (success) {
        tui.showMessage("Database tables cleared successfully!", 2000);
    } else {
        tui.showError("Failed to clear database tables");
    }
    
    return success;
}

bool InteractiveManager::handleDatabaseStatus() {
    tui.updateStatus("Retrieving database status...");
    
    // Switch to console briefly to show status
    tui.cleanup();
    
    std::cout << "\n=== Database Status ===" << std::endl;
    std::cout << "Testing database connection..." << std::endl;
    
    if (db_manager.initializeDatabase()) {
        std::cout << "✓ Database connection: OK" << std::endl;
        
        // Check table status
        std::vector<std::string> tables = {"ndc_data", "drugsfda_data", "drug_label_data", "fda_ndc_data"};
        for (const auto& table : tables) {
            if (db_manager.tableExists(table)) {
                std::cout << "✓ Table " << table << ": EXISTS" << std::endl;
            } else {
                std::cout << "✗ Table " << table << ": NOT FOUND" << std::endl;
            }
        }
    } else {
        std::cout << "✗ Database connection: FAILED" << std::endl;
    }
    
    std::cout << "\nPress Enter to continue...";
    std::cin.get();
    
    // Reinitialize TUI
    if (!tui.initialize()) {
        return false;
    }
    
    return true;
}

bool InteractiveManager::handleEditDatabaseConfig() {
    // Create a temporary config for editing
    DatabaseConfig config;
    
    if (tui.editDatabaseConfig(config)) {
        std::string filename = tui.getStringInput("Configuration filename:", "database.nfo");
        if (!filename.empty()) {
            return tui.saveDatabaseConfig(config, filename);
        }
    }
    
    return false;
}

bool InteractiveManager::handleSaveCurrentConfig() {
    std::string filename = tui.getStringInput("Configuration filename:", "database.nfo");
    if (filename.empty()) {
        return false;
    }
    
    tui.showMessage("Note: Current configuration will be saved", 2000);
    
    // Create a placeholder config - in real implementation, get current config from db_manager
    DatabaseConfig config;
    config.host = "localhost";
    config.port = 3306;
    config.user = "root";
    config.database = "openfda";
    config.schema = "openfda";
    
    return tui.saveDatabaseConfig(config, filename);
}

bool InteractiveManager::handleLoadConfigFile() {
    std::string filename = tui.getStringInput("Configuration filename:", "database.nfo");
    if (filename.empty()) {
        return false;
    }
    
    if (db_manager.loadDatabaseConfig(filename)) {
        tui.showMessage("Configuration loaded successfully!", 2000);
        return true;
    } else {
        tui.showError("Failed to load configuration file");
        return false;
    }
}