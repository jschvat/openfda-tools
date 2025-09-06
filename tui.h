#pragma once

#include "types.h"
#include <ncurses.h>
#include <menu.h>
#include <string>
#include <vector>

class AdvancedTUI {
private:
    WINDOW* main_window;
    WINDOW* status_window;
    int height, width;
    bool initialized;

public:
    AdvancedTUI();
    ~AdvancedTUI();
    
    bool initialize();
    void cleanup();
    
    // Menu operations
    int showMenu(const std::vector<std::string>& options, const std::string& title, int min_width = 80);
    int showMainMenu();
    
    // Dialog operations
    bool showYesNoDialog(const std::string& question);
    std::string getStringInput(const std::string& prompt, const std::string& default_value = "");
    
    // Database connection management
    bool editDatabaseConfig(DatabaseConfig& config);
    bool saveDatabaseConfig(const DatabaseConfig& config, const std::string& filename);
    void showDatabaseConfigSummary(const DatabaseConfig& config);
    
    // Configuration display
    void showConfigSummary(DataSource dataSource, bool clearDatabase, const std::string& configFile);
    void showOperationSummary(const std::string& operation, const std::string& details);
    
    // Status and messages
    void showMessage(const std::string& message, int delay_ms = 2000);
    void showError(const std::string& error, int delay_ms = 3000);
    void updateStatus(const std::string& status);
    
    // Utility methods
    void centerText(int y, const std::string& text);
    void drawBorder();
    void drawTitle(const std::string& title);
    
private:
    void initColors();
    std::string trimString(const std::string& str);
    int getMenuWidth(const std::vector<std::string>& options, const std::string& title, int min_width);
};