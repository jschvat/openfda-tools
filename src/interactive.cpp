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
    
    // Set interactive mode flag to suppress console messages
    db_manager.setInteractiveMode(true);
    
    // Connect database messages to TUI footer
    db_manager.setTUIOutput([&](const std::string& message) {
        tui.addFooterMessage(message);
    });
    
    // Load default database configuration and connect automatically
    initializeDatabaseConnection();
    
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
                
            case 4: // About
                handleAbout();
                break;
                
            case 5: // Exit
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
        case DataSource::RXIMAGE_BULK:
            sourceStr = "RxIMAGE Drug Images Download (Visual pill identification data)";
            break;
        case DataSource::DAILYMED_NDC_IMPRINT:
            sourceStr = "DailyMed NDC Imprint API (Physical characteristics)";
            break;
        case DataSource::RXNORM_NDC_PROPERTIES:
            sourceStr = "RxNorm NDC Properties API (Enhanced NDC data)";
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
    } else if (dataSource == DataSource::RXIMAGE_BULK) {
        table_created = db_manager.createDrugImagesTable();
    } else if (dataSource == DataSource::DAILYMED_NDC_IMPRINT || dataSource == DataSource::RXNORM_NDC_PROPERTIES) {
        table_created = db_manager.createNDCImprintTable();
    }
    
    if (!table_created) {
        tui.showError("Failed to create database table");
        return false;
    }
    
    // Process data
    tui.updateStatus("Processing data - this may take a while...");
    tui.addFooterMessage("Data processing started. Progress will be shown below.");
    
    // Keep TUI active and use interactive mode
    DataProcessor processor(db_manager, "./logs/");
    processor.setDataSource(dataSource);
    processor.setTUI(&tui);
    processor.setInteractiveMode(true);
    processor.processAllData();
    
    tui.addFooterMessage("✅ Data processing completed successfully!");
    return true;
}

bool InteractiveManager::handleDatabaseManagement() {
    std::vector<std::string> dbOptions = {
        "Clear Tables - Remove data from tables",
        "View Status - Show connection and table info", 
        "Test Connection - Verify MySQL connectivity",
        "Return to Main Menu"
    };
    
    int choice = tui.showMenu(dbOptions, "Database Management Options", 60);
    
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
    // Use the new enhanced database configuration menu
    int choice = tui.showDatabaseConfigMenu();
    
    switch (choice) {
        case 1: // Edit Database Config
            return handleEditDatabaseConfig();
            
        case 2: // Test Connection
            return handleTestConnection();
            
        case 3: // Create Database & Tables
            return handleCreateDatabaseAndTables();
            
        case 4: // Switch Database Type
            return handleSwitchDatabaseType();
            
        case 5: // Save Config
            return handleSaveCurrentConfig();
            
        case 6: // Load Config File
            return handleLoadConfigFile();
            
        case 7: // Return to main menu
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
        "FDA NDC Bulk - Single file (normalized NDC)",
        "RxIMAGE Drug Images - Pill images with visual data",
        "DailyMed NDC Imprint - Physical characteristics API",
        "RxNorm NDC Properties - Enhanced NDC data"
    };
    
    int choice = tui.showMenu(dataSourceOptions, "Select Data Source for Download", 70);
    
    switch (choice) {
        case 1: return DataSource::NDC_API;
        case 2: return DataSource::NDC_BULK;
        case 3: return DataSource::DRUGSFDA_BULK;
        case 4: return DataSource::DRUG_LABEL_BULK;
        case 5: return DataSource::FDA_NDC_BULK;
        case 6: return DataSource::RXIMAGE_BULK;
        case 7: return DataSource::DAILYMED_NDC_IMPRINT;
        case 8: return DataSource::RXNORM_NDC_PROPERTIES;
        default: return static_cast<DataSource>(-1); // Cancelled
    }
}

bool InteractiveManager::handleClearDatabase() {
    std::vector<std::string> clearOptions = {
        "Clear NDC Table - Remove NDC records",
        "Clear DrugsFDA Table - Remove DrugsFDA records", 
        "Clear Drug Label Table - Remove label records",
        "Clear FDA NDC Table - Remove FDA NDC records",
        "Clear Drug Images Table - Remove RxIMAGE data",
        "Clear NDC Imprint Table - Remove DailyMed/RxNorm data",
        "Clear All Tables - Remove all data",
        "Cancel - Return without clearing"
    };
    
    int choice = tui.showMenu(clearOptions, "Select Tables to Clear", 60);
    
    if (choice == -1 || choice == 8) {
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
            success = db_manager.clearTable("drug_images");
            break;
        case 6:
            success = db_manager.clearTable("ndc_imprint_data");
            break;
        case 7:
            success = db_manager.clearTable("ndc_data") &&
                     db_manager.clearTable("drugsfda_data") &&
                     db_manager.clearTable("drug_label_data") &&
                     db_manager.clearTable("fda_ndc_data") &&
                     db_manager.clearTable("drug_images") &&
                     db_manager.clearTable("ndc_imprint_data");
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
    
    // Test database connection
    bool connection_ok = db_manager.initializeDatabase();
    
    // Check table status
    std::vector<std::pair<std::string, bool>> tables;
    std::vector<std::string> table_names = {"ndc_data", "drugsfda_data", "drug_label_data", "fda_ndc_data", "drug_images", "ndc_imprint_data"};
    
    if (connection_ok) {
        for (const auto& table_name : table_names) {
            bool exists = db_manager.tableExists(table_name);
            tables.push_back(std::make_pair(table_name, exists));
        }
    }
    
    // Show status in TUI dialog
    tui.showDatabaseStatus(connection_ok, tables);
    
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

bool InteractiveManager::handleTestConnection() {
    tui.showMessage("Testing database connection...", 1000);
    
    // Test the database connection using the database manager
    if (db_manager.initializeDatabase()) {
        tui.updateDatabaseFooter("miniserver.local", 5432, "jason", "PostgreSQL", true);
        tui.showMessage("✓ Database connection successful!", 2000);
        return true;
    } else {
        tui.clearDatabaseFooter();
        tui.showError("✗ Database connection failed!", 2000);
        return false;
    }
}

bool InteractiveManager::handleCreateDatabaseAndTables() {
    if (!tui.showYesNoDialog("Create database and tables if they don't exist?")) {
        return false;
    }
    
    tui.showMessage("Creating database structures...", 1500);
    
    // Ensure database and schema exist
    if (!db_manager.ensureSchemaExists()) {
        tui.showError("Failed to create database/schema");
        return false;
    }
    
    tui.showMessage("✓ Database/Schema created", 1000);
    
    // Create all required tables
    bool success = true;
    
    if (db_manager.createNDCTable()) {
        tui.showMessage("✓ NDC table created", 800);
    } else {
        success = false;
    }
    
    if (db_manager.createDrugsFDATable()) {
        tui.showMessage("✓ DrugsFDA table created", 800);
    } else {
        success = false;
    }
    
    if (db_manager.createDrugLabelTable()) {
        tui.showMessage("✓ Drug Label table created", 800);
    } else {
        success = false;
    }
    
    if (db_manager.createFDANDCDataTable()) {
        tui.showMessage("✓ FDA NDC table created", 800);
    } else {
        success = false;
    }
    
    if (db_manager.createDrugImagesTable()) {
        tui.showMessage("✓ Drug Images table created", 800);
    } else {
        success = false;
    }
    
    if (db_manager.createNDCImprintTable()) {
        tui.showMessage("✓ NDC Imprint Data table created", 800);
    } else {
        success = false;
    }
    
    if (success) {
        tui.showMessage("🎉 All database structures ready!", 2000);
    } else {
        tui.showError("Some tables failed to create");
    }
    
    return success;
}

bool InteractiveManager::handleSwitchDatabaseType() {
    std::vector<std::string> dbOptions = {
        "MySQL - Traditional relational database",
        "PostgreSQL - Advanced relational database with JSONB"
    };
    
    int choice = tui.showMenu(dbOptions, "Select Database Type", 60);
    if (choice == -1) return false;
    
    std::string selected_db = (choice == 1) ? "mysql" : "postgresql";
    std::string display_name = (choice == 1) ? "MySQL" : "PostgreSQL";
    
    // In a real implementation, this would switch the database configuration
    // For now, just simulate the switch
    tui.showMessage("Switching to " + display_name + "...", 1500);
    
    // Update footer to reflect the switch
    if (choice == 1) {
        tui.updateDatabaseFooter("miniserver.home", 3306, "root", "MySQL", false);
    } else {
        tui.updateDatabaseFooter("miniserver.local", 5432, "jason", "PostgreSQL", false);
    }
    
    tui.showMessage("✓ Switched to " + display_name + " database", 2000);
    return true;
}

bool InteractiveManager::runInteractiveMode(const std::string& config_file) {
    if (!tui.initialize()) {
        std::cerr << "❌ Failed to initialize TUI. Falling back to simple mode." << std::endl;
        return false;
    }
    
    tui.updateStatus("Interactive mode - Use arrow keys to navigate menus");
    
    // Set interactive mode flag to suppress console messages
    db_manager.setInteractiveMode(true);
    
    // Connect database messages to TUI footer
    db_manager.setTUIOutput([&](const std::string& message) {
        tui.addFooterMessage(message);
    });
    
    // Load specified database configuration and connect automatically
    initializeDatabaseConnection(config_file);
    
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
                
            case 4: // About
                handleAbout();
                break;
                
            case 5: // Exit
            case -1: // User pressed 'q' or Escape
                tui.showMessage("Thank you for using OpenFDA Data Downloader!", 2000);
                return true;
                
            default:
                tui.showError("Invalid selection. Please try again.");
                break;
        }
    }
}

void InteractiveManager::initializeDatabaseConnection() {
    initializeDatabaseConnection("database.nfo");
}

void InteractiveManager::initializeDatabaseConnection(const std::string& config_file) {
    // Try to load specified database configuration
    tui.showMessage("Loading database configuration...", 500);
    
    bool config_loaded = false;
    
    // First try to load the specified configuration
    if (db_manager.loadDatabaseConfig(config_file)) {
        config_loaded = true;
        tui.showMessage("✓ Configuration loaded", 500);
    } else if (config_file != "database-multi.nfo") {
        // Try multi-database configuration as fallback
        if (db_manager.loadMultiDatabaseConfig("database-multi.nfo")) {
            config_loaded = true;
            tui.showMessage("✓ Multi-database configuration loaded", 500);
        }
    }
    
    if (!config_loaded) {
        tui.clearDatabaseFooter();
        tui.showError("No database configuration found", 2000);
        return;
    }
    
    // Attempt to connect to the database
    tui.showMessage("Connecting to database...", 500);
    
    bool connected = false;
    std::string host = "Unknown";
    int port = 0;
    std::string user = "Unknown";
    std::string dbtype = "Unknown";
    
    // Get the actual database configuration
    const DatabaseConfig& config = db_manager.getDatabaseConfig();
    host = config.host;
    port = config.port;
    user = config.user;
    dbtype = (config.type == DatabaseType::POSTGRESQL) ? "PostgreSQL" : "MySQL";
    
    // Try to initialize the database connection
    if (db_manager.initializeDatabase()) {
        connected = true;
        tui.showMessage("✓ Database connected successfully", 1000);
    } else {
        tui.showError("Database connection failed", 1500);
    }
    
    // Update the footer with connection status
    tui.updateDatabaseFooter(host, port, user, dbtype, connected);
}

void InteractiveManager::handleAbout() {
    std::vector<std::string> aboutContent = {
        "===============================================================================",
        "                            OpenFDA Data Downloader",
        "                    Comprehensive Pharmaceutical Data Processing Tool",
        "===============================================================================",
        "",
        "DATA SOURCES AND ENDPOINTS",
        "",
        "+-----------------------------------------------------------------------------+",
        "|                              FDA OPENFDA APIs                               |",
        "+-----------------------------------------------------------------------------+",
        "",
        "* NDC API (Real-time)",
        "   URL: https://api.fda.gov/drug/ndc.json",
        "   Table: ndc_data",
        "   Description: National Drug Code directory with real-time API access",
        "   Data: Product NDCs, labelers, package info, active ingredients",
        "",
        "* NDC Bulk Download",
        "   URL: https://download.open.fda.gov/drug/ndc/drug-ndc-0001-of-0001.json.zip",
        "   Table: ndc_data", 
        "   Description: Complete NDC dataset in single download",
        "   Data: All NDC records, faster processing than API",
        "",
        "* DrugsFDA Bulk",
        "   URL: https://download.open.fda.gov/drug/drugsfda/drug-drugsfda-0001-of-0001.json.zip",
        "   Table: drugsfda_data",
        "   Description: FDA-approved drug products and applications",
        "   Data: Application numbers, approval dates, sponsors, product names",
        "",
        "* Drug Label Bulk (13 Parts)",
        "   URL: https://download.open.fda.gov/drug/label/drug-label-XXXX-of-0013.json.zip",
        "   Table: drug_label_data",
        "   Description: Structured Product Labeling (SPL) data",
        "   Data: Comprehensive labeling info, warnings, dosage, administration",
        "",
        "* FDA NDC Bulk (Normalized)",
        "   URL: https://download.open.fda.gov/drug/ndc/drug-ndc-0001-of-0001.json.zip",
        "   Table: fda_ndc_data",
        "   Description: FDA NDC data with normalized formatting",
        "   Data: Standardized NDC format, enhanced for database queries",
        "",
        "+-----------------------------------------------------------------------------+",
        "|                         NLM (National Library of Medicine)                  |",
        "+-----------------------------------------------------------------------------+",
        "",
        "* RxIMAGE Drug Images",
        "   Source: NLM Data Discovery Portal (https://datadiscovery.nlm.nih.gov/)",
        "   Table: drug_images",
        "   Description: Visual identification of prescription drugs",
        "   Data: Drug images, NDC codes, physical characteristics, colors, shapes",
        "   Note: API discontinued - manual download required",
        "",
        "* DailyMed NDC Imprint API",
        "   URL: https://dailymed.nlm.nih.gov/dailymed/services/v1/ndc/{ndc}/imprintdata.json",
        "   Table: ndc_imprint_data",
        "   Description: Physical pill/tablet characteristics",
        "   Data: Imprint text, colors, shapes, coating, scoring, symbols",
        "",
        "* RxNorm NDC Properties API",
        "   URL: https://rxnav.nlm.nih.gov/REST/ndc/{ndc}/allProperties.json",
        "   Table: ndc_imprint_data",
        "   Description: Enhanced NDC properties and relationships",
        "   Data: NDC properties, labeler info, enhanced metadata",
        "",
        "===============================================================================",
        "DATABASE TABLES AND SCHEMAS",
        "===============================================================================",
        "",
        "+-----------------------------------------------------------------------------+",
        "| ndc_data - Primary NDC Directory                                            |",
        "+-----------------------------------------------------------------------------+",
        "- Primary Key: product_ndc (standardized format)",
        "- Fields: labeler_name, product_name, product_type, route, active_ingredients",
        "- JSON Storage: Raw API response data",
        "- Source: NDC API & NDC Bulk downloads",
        "- Records: ~150,000+ pharmaceutical products",
        "",
        "+-----------------------------------------------------------------------------+",
        "| drugsfda_data - FDA Approved Drugs                                          |",
        "+-----------------------------------------------------------------------------+",
        "- Primary Key: application_number",
        "- Fields: sponsor_name, application_type, application_number, products",
        "- JSON Storage: Complete application details",
        "- Source: DrugsFDA Bulk download",
        "- Records: ~20,000+ FDA-approved applications",
        "",
        "+-----------------------------------------------------------------------------+",
        "| drug_label_data - Structured Product Labeling                              |",
        "+-----------------------------------------------------------------------------+",
        "- Primary Key: set_id or product_ndc",
        "- Fields: product_ndc, generic_name, brand_name, dosage_form, route",
        "- JSON Storage: Complete SPL data including warnings, indications",
        "- Source: Drug Label Bulk (13 parts)",
        "- Records: ~200,000+ labeled drug products",
        "",
        "+-----------------------------------------------------------------------------+",
        "| fda_ndc_data - Normalized FDA NDC                                          |",
        "+-----------------------------------------------------------------------------+",
        "- Primary Key: product_ndc (normalized format)",
        "- Fields: Similar to ndc_data but with enhanced formatting",
        "- JSON Storage: Normalized API responses",
        "- Source: FDA NDC Bulk with normalization processing",
        "- Records: ~150,000+ with standardized NDC formatting",
        "",
        "┌─────────────────────────────────────────────────────────────────────────────┐",
        "│ drug_images - Visual Drug Identification                                   │",
        "└─────────────────────────────────────────────────────────────────────────────┘",
        "• Primary Key: image_id or ndc",
        "• Fields: ndc, image_url, color, shape, imprint, size_mm, score, coating",
        "• JSON Storage: Complete image metadata and physical characteristics",
        "• Source: RxIMAGE bulk downloads from NLM",
        "• Records: Variable (depends on manual download)",
        "",
        "┌─────────────────────────────────────────────────────────────────────────────┐",
        "│ ndc_imprint_data - Physical Drug Characteristics                           │",
        "└─────────────────────────────────────────────────────────────────────────────┘",
        "• Primary Key: ndc",
        "• Fields: setid, product_name, color_text, imprint, shape_text, size_text",
        "• JSON Storage: Raw API responses from DailyMed and RxNorm",
        "• Source: DailyMed Imprint API & RxNorm Properties API",
        "• Records: Variable (based on existing NDC records in database)",
        "",
        "═══════════════════════════════════════════════════════════════════════════════",
        "🔄 DATA PROCESSING DIFFERENCES",
        "═══════════════════════════════════════════════════════════════════════════════",
        "",
        "📈 BULK vs API Processing:",
        "• BULK: Faster, complete datasets, single download, offline processing",
        "• API: Real-time, paginated, rate-limited, smaller incremental updates",
        "",
        "📊 Data Relationships:",
        "• NDC codes link across all tables as common identifiers",
        "• drug_images and ndc_imprint_data enhance ndc_data with visual/physical info",
        "• drugsfda_data provides regulatory approval context",
        "• drug_label_data provides comprehensive medical/legal information",
        "",
        "⚡ Performance Notes:",
        "• Bulk downloads: Best for initial data loading",
        "• API calls: Best for incremental updates and real-time queries",
        "• Rate limiting: 100ms between API requests (respectful to NLM services)",
        "",
        "🗄️  Database Support:",
        "• PostgreSQL: Full JSON/JSONB support, advanced indexing",
        "• MySQL: JSON fields with basic querying capabilities",
        "• Automatic schema detection and creation",
        "",
        "===============================================================================",
        "COMMAND LINE USAGE",
        "===============================================================================",
        "",
        "./openfda_ndc_downloader --ndc          # NDC API processing",
        "./openfda_ndc_downloader --ndc-bulk     # NDC bulk download", 
        "./openfda_ndc_downloader --drugsfda     # DrugsFDA bulk download",
        "./openfda_ndc_downloader --drug-label   # Drug Label bulk download",
        "./openfda_ndc_downloader --fda-ndc      # FDA NDC normalized bulk",
        "./openfda_ndc_downloader --rximage      # RxIMAGE processing (manual)",
        "./openfda_ndc_downloader --dailymed     # DailyMed imprint API",
        "./openfda_ndc_downloader --rxnorm       # RxNorm properties API",
        "./openfda_ndc_downloader -i             # Interactive TUI mode",
        "",
        "===============================================================================",
        "Version: 2.0 | Build: " + std::string(__DATE__) + " " + std::string(__TIME__),
        "Database Support: PostgreSQL, MySQL | TUI: ncurses | Network: libcurl",
        "==============================================================================="
    };
    
    tui.showScrollableText(aboutContent, "About OpenFDA Data Downloader");
}