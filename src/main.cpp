#include "types.h"
#include "database.h"
#include "data_processor.h"
#include "network.h"
#include "interactive.h"
#include <iostream>
#include <getopt.h>
#include <string>

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] [config_file]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -n, --ndc         Use NDC API (default)" << std::endl;
    std::cout << "  -b, --ndc-bulk    Use NDC bulk download" << std::endl;
    std::cout << "  -d, --drugsfda    Use DrugsFDA bulk download" << std::endl;
    std::cout << "  -l, --drug-label  Use Drug Label bulk download (13 parts)" << std::endl;
    std::cout << "  -f, --fda-ndc     Use FDA NDC bulk download (normalized format)" << std::endl;
    std::cout << "  --rximage         Use RxIMAGE bulk download from NLM" << std::endl;
    std::cout << "  --dailymed        Use DailyMed NDC imprint data API" << std::endl;
    std::cout << "  --rxnorm          Use RxNorm NDC properties API" << std::endl;
    std::cout << "  -x, --clear       Clear database tables before processing" << std::endl;
    std::cout << "  --clear-ndc       Clear only NDC data table" << std::endl;
    std::cout << "  --clear-drugsfda  Clear only DrugsFDA data table" << std::endl;
    std::cout << "  --clear-labels    Clear only Drug Label data table" << std::endl;
    std::cout << "  --clear-fda-ndc   Clear only FDA NDC data table" << std::endl;
    std::cout << "  -i, --interactive Interactive TUI mode" << std::endl;
    std::cout << "  -c, --config      Database config file (default: database.nfo)" << std::endl;
    std::cout << "  --no-error-log    Disable error and duplicate logging to JSON files" << std::endl;
    std::cout << "  --log-dir         Directory for error log files (default: ./logs/)" << std::endl;
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
    std::cout << "  " << program_name << " --rximage         # RxIMAGE bulk download from NLM" << std::endl;
    std::cout << "  " << program_name << " --dailymed        # DailyMed NDC imprint API" << std::endl;
    std::cout << "  " << program_name << " --rxnorm          # RxNorm NDC properties API" << std::endl;
    std::cout << "  " << program_name << " --clear --fda-ndc # Clear all tables, then FDA NDC bulk" << std::endl;
    std::cout << "  " << program_name << " --clear-ndc --ndc-bulk # Clear NDC table only, then bulk load" << std::endl;
    std::cout << "  " << program_name << " --clear-labels --drug-label # Clear labels only, then load" << std::endl;
    std::cout << "  " << program_name << " -c mydb.conf      # Custom config file" << std::endl;
    std::cout << "  " << program_name << " -i                # Interactive TUI mode" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default settings
    DataSource data_source = DataSource::NDC_API;
    bool clear_database = false;
    bool clear_ndc_table = false;
    bool clear_drugsfda_table = false;
    bool clear_labels_table = false;
    bool clear_fda_ndc_table = false;
    bool interactive_mode = false;
    bool error_logging_enabled = true;
    std::string config_file = "database.nfo";
    std::string log_directory = "./logs/";
    
    // Command line parsing
    int c;
    int option_index = 0;
    
    struct option long_options[] = {
        {"ndc",         no_argument,       0, 'n'},
        {"ndc-bulk",    no_argument,       0, 'b'},
        {"drugsfda",    no_argument,       0, 'd'},
        {"drug-label",  no_argument,       0, 'l'},
        {"fda-ndc",     no_argument,       0, 'f'},
        {"rximage",     no_argument,       0, 1007},
        {"dailymed",    no_argument,       0, 1008},
        {"rxnorm",      no_argument,       0, 1009},
        {"clear",       no_argument,       0, 'x'},
        {"clear-ndc",   no_argument,       0, 1003},
        {"clear-drugsfda", no_argument,    0, 1004},
        {"clear-labels", no_argument,      0, 1005},
        {"clear-fda-ndc", no_argument,     0, 1006},
        {"interactive", no_argument,       0, 'i'},
        {"config",      required_argument, 0, 'c'},
        {"no-error-log", no_argument,      0, 1001},
        {"log-dir",     required_argument, 0, 1002},
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
            case 1001: // --no-error-log
                error_logging_enabled = false;
                break;
            case 1002: // --log-dir
                log_directory = optarg;
                break;
            case 1003: // --clear-ndc
                clear_ndc_table = true;
                break;
            case 1004: // --clear-drugsfda
                clear_drugsfda_table = true;
                break;
            case 1005: // --clear-labels
                clear_labels_table = true;
                break;
            case 1006: // --clear-fda-ndc
                clear_fda_ndc_table = true;
                break;
            case 1007: // --rximage
                data_source = DataSource::RXIMAGE_BULK;
                break;
            case 1008: // --dailymed
                data_source = DataSource::DAILYMED_NDC_IMPRINT;
                break;
            case 1009: // --rxnorm
                data_source = DataSource::RXNORM_NDC_PROPERTIES;
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
    
    // Initialize network layer
    if (!NetworkManager::globalInit()) {
        std::cerr << "❌ Failed to initialize network layer" << std::endl;
        return 1;
    }
    
    // Initialize database manager
    DatabaseManager db_manager;
    
    // Run interactive mode if requested
    if (interactive_mode) {
        InteractiveManager interactive(db_manager);
        bool success = interactive.runInteractiveMode(config_file);
        NetworkManager::globalCleanup();
        return success ? 0 : 1;
    }
    
    // Non-interactive mode
    std::cout << "🚀 OpenFDA Data Downloader" << std::endl;
    std::cout << "===============================================" << std::endl;
    
    // Load configuration
    if (!db_manager.loadDatabaseConfig(config_file)) {
        NetworkManager::globalCleanup();
        return 1;
    }
    
    // Initialize database
    if (!db_manager.initializeDatabase()) {
        NetworkManager::globalCleanup();
        return 1;
    }
    
    if (!db_manager.ensureSchemaExists()) {
        NetworkManager::globalCleanup();
        return 1;
    }
    
    // Set data source for database manager
    db_manager.setDataSource(data_source);
    
    // Create appropriate table
    bool table_created = false;
    if (data_source == DataSource::NDC_API || data_source == DataSource::NDC_BULK) {
        table_created = db_manager.createNDCTable();
        if (data_source == DataSource::NDC_API) {
            std::cout << "🔄 Processing NDC data from API..." << std::endl;
        } else {
            std::cout << "🔄 Processing NDC data from bulk download..." << std::endl;
        }
    } else if (data_source == DataSource::DRUG_LABEL_BULK) {
        table_created = db_manager.createDrugLabelTable();
        std::cout << "🔄 Processing Drug Label data from bulk download..." << std::endl;
    } else if (data_source == DataSource::FDA_NDC_BULK) {
        table_created = db_manager.createFDANDCDataTable();
        std::cout << "🔄 Processing FDA NDC data from bulk download..." << std::endl;
    } else if (data_source == DataSource::RXIMAGE_BULK) {
        table_created = db_manager.createDrugImagesTable();
        std::cout << "🔄 Processing RxIMAGE drug image data from NLM..." << std::endl;
    } else if (data_source == DataSource::DAILYMED_NDC_IMPRINT) {
        table_created = db_manager.createNDCImprintTable();
        std::cout << "🔄 Processing DailyMed NDC imprint data from API..." << std::endl;
    } else if (data_source == DataSource::RXNORM_NDC_PROPERTIES) {
        table_created = db_manager.createNDCImprintTable();
        std::cout << "🔄 Processing RxNorm NDC properties from API..." << std::endl;
    } else {
        table_created = db_manager.createDrugsFDATable();
        std::cout << "🔄 Processing DrugsFDA data from bulk download..." << std::endl;
    }
    
    if (!table_created) {
        NetworkManager::globalCleanup();
        return 1;
    }
    
    // Clear database if requested
    if (clear_database) {
        if (!db_manager.clearDatabase()) {
            NetworkManager::globalCleanup();
            return 1;
        }
    }
    
    // Clear individual tables if requested
    if (clear_ndc_table) {
        std::cout << "🗑️  Clearing NDC data table..." << std::endl;
        if (!db_manager.clearTable("ndc_data")) {
            std::cerr << "❌ Failed to clear NDC data table" << std::endl;
            NetworkManager::globalCleanup();
            return 1;
        }
    }
    
    if (clear_drugsfda_table) {
        std::cout << "🗑️  Clearing DrugsFDA data table..." << std::endl;
        if (!db_manager.clearTable("drugsfda_data")) {
            std::cerr << "❌ Failed to clear DrugsFDA data table" << std::endl;
            NetworkManager::globalCleanup();
            return 1;
        }
    }
    
    if (clear_labels_table) {
        std::cout << "🗑️  Clearing Drug Label data table..." << std::endl;
        if (!db_manager.clearTable("drug_label_data")) {
            std::cerr << "❌ Failed to clear Drug Label data table" << std::endl;
            NetworkManager::globalCleanup();
            return 1;
        }
    }
    
    if (clear_fda_ndc_table) {
        std::cout << "🗑️  Clearing FDA NDC data table..." << std::endl;
        if (!db_manager.clearTable("fda_ndc_data")) {
            std::cerr << "❌ Failed to clear FDA NDC data table" << std::endl;
            NetworkManager::globalCleanup();
            return 1;
        }
    }
    
    // Process data
    std::cout << "✅ Starting data processing..." << std::endl;
    if (!error_logging_enabled) {
        std::cout << "ℹ️  Error logging disabled" << std::endl;
    } else {
        std::cout << "📝 Error logs will be saved to: " << log_directory << std::endl;
    }
    std::cout << std::endl;
    
    DataProcessor processor(db_manager, log_directory);
    processor.setDataSource(data_source);
    processor.setErrorLoggingEnabled(error_logging_enabled);
    processor.processAllData();
    
    NetworkManager::globalCleanup();
    return 0;
}