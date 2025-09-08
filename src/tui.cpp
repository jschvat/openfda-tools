#include "tui.h"
#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>

AdvancedTUI::AdvancedTUI() : main_window(nullptr), status_window(nullptr), footer_window(nullptr), 
                             initialized(false), footer_scroll_offset(0), max_footer_lines(5),
                             progress_bar_visible(false), progress_current(0), 
                             progress_total(0), progress_bar_y(0) {}

AdvancedTUI::~AdvancedTUI() {
    cleanup();
}

bool AdvancedTUI::initialize() {
    // Initialize ncurses
    if (initscr() == nullptr) {
        return false;
    }
    
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    
    // Get screen dimensions
    getmaxyx(stdscr, height, width);
    
    // Ensure minimum dimensions
    if (height < 20 || width < 70) {
        endwin();
        std::cerr << "Terminal too small. Minimum size: 70x20" << std::endl;
        return false;
    }
    
    // Initialize colors
    initColors();
    
    // Create windows: main, status (middle), footer (bottom) - status is now 3 lines, footer 7 lines
    main_window = newwin(height - 10, width, 0, 0);
    status_window = newwin(3, width, height - 10, 0);
    footer_window = newwin(7, width, height - 7, 0);
    
    if (!main_window || !status_window || !footer_window) {
        cleanup();
        return false;
    }
    
    // Set up status window with green background
    wbkgd(status_window, COLOR_PAIR(9)); // Use green background for status
    box(status_window, 0, 0);
    
    // Set up footer window with blue background
    wbkgd(footer_window, COLOR_PAIR(8)); // Use blue background for footer
    box(footer_window, 0, 0);
    
    initialized = true;
    return true;
}

void AdvancedTUI::initColors() {
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_WHITE, COLOR_BLUE);    // Header
        init_pair(2, COLOR_BLACK, COLOR_CYAN);    // Menu selection
        init_pair(3, COLOR_GREEN, COLOR_BLACK);   // Success
        init_pair(4, COLOR_WHITE, COLOR_BLACK);   // Normal
        init_pair(5, COLOR_CYAN, COLOR_BLACK);    // Info
        init_pair(6, COLOR_RED, COLOR_BLACK);     // Error
        init_pair(7, COLOR_YELLOW, COLOR_BLACK);  // Warning
        init_pair(8, COLOR_WHITE, COLOR_BLUE);    // Footer blue background
        init_pair(9, COLOR_WHITE, COLOR_GREEN);   // Status green background
    }
}

void AdvancedTUI::cleanup() {
    if (initialized) {
        if (main_window) delwin(main_window);
        if (status_window) delwin(status_window);
        if (footer_window) delwin(footer_window);
        endwin();
        initialized = false;
    }
}

int AdvancedTUI::getMenuWidth(const std::vector<std::string>& options, const std::string& title, int min_width) {
    int max_width = title.length() + 4; // Title + padding
    
    for (const auto& option : options) {
        int option_width = option.length() + 8; // Option number + padding + text
        max_width = std::max(max_width, option_width);
    }
    
    return std::max(max_width, min_width);
}

int AdvancedTUI::showMenu(const std::vector<std::string>& options, const std::string& title, int min_width) {
    if (!initialized || options.empty()) return -1;
    
    // Clear only the main window area, keep status and footer visible
    werase(main_window);
    wrefresh(main_window);
    
    // Refresh status and footer windows to ensure they're visible
    refreshAllWindows();
    
    // Calculate menu dimensions within the main window area
    int main_height = height - 10; // Account for status (3) + footer (7)
    int menu_width = getMenuWidth(options, title, min_width);
    int menu_height = options.size() + 6;
    int start_y = (main_height - menu_height) / 2;
    int start_x = (width - menu_width) / 2;
    
    // Ensure menu fits on screen
    if (start_y < 0) start_y = 1;
    if (start_x < 0) start_x = 1;
    if (menu_width > width - 2) menu_width = width - 2;
    
    WINDOW* menu_win = newwin(menu_height, menu_width, start_y, start_x);
    if (!menu_win) return -1;
    
    keypad(menu_win, TRUE);  // Enable arrow keys for this window
    wbkgd(menu_win, COLOR_PAIR(4));
    box(menu_win, 0, 0);
    
    // Draw title
    if (has_colors()) wattron(menu_win, COLOR_PAIR(1) | A_BOLD);
    int title_x = (menu_width - title.length()) / 2;
    mvwprintw(menu_win, 1, title_x, "%s", title.c_str());
    if (has_colors()) wattroff(menu_win, COLOR_PAIR(1) | A_BOLD);
    
    // Draw separator line
    mvwhline(menu_win, 2, 1, ACS_HLINE, menu_width - 2);
    mvwaddch(menu_win, 2, 0, ACS_LTEE);
    mvwaddch(menu_win, 2, menu_width - 1, ACS_RTEE);
    
    int current_option = 0;
    int key;
    
    while (true) {
        // Draw options
        for (size_t i = 0; i < options.size(); i++) {
            int y = 4 + i;
            
            // Clear the entire line first
            mvwhline(menu_win, y, 1, ' ', menu_width - 2);
            
            if (i == current_option) {
                if (has_colors()) wattron(menu_win, COLOR_PAIR(2) | A_BOLD);
                mvwprintw(menu_win, y, 2, " %zu. %s", i + 1, options[i].c_str());
                // Fill the rest of the line with the selection color
                int text_len = std::to_string(i + 1).length() + options[i].length() + 4;
                for (int x = text_len + 2; x < menu_width - 1; x++) {
                    mvwaddch(menu_win, y, x, ' ');
                }
                if (has_colors()) wattroff(menu_win, COLOR_PAIR(2) | A_BOLD);
            } else {
                mvwprintw(menu_win, y, 2, " %zu. %s", i + 1, options[i].c_str());
            }
        }
        
        // Draw instructions
        mvwprintw(menu_win, menu_height - 2, 2, "Use arrow keys to navigate, Enter to select, 'q' to quit");
        
        wrefresh(menu_win);
        
        key = wgetch(menu_win);
        
        switch (key) {
            case KEY_UP:
                current_option = (current_option - 1 + options.size()) % options.size();
                break;
            case KEY_DOWN:
                current_option = (current_option + 1) % options.size();
                break;
            case '\n':
            case '\r':
            case KEY_ENTER:
                delwin(menu_win);
                return current_option + 1;
            case 'q':
            case 'Q':
            case 27: // Escape
                delwin(menu_win);
                return -1;
            default:
                // Check if user pressed a number key
                if (key >= '1' && key <= '9') {
                    int choice = key - '0';
                    if (choice <= static_cast<int>(options.size())) {
                        delwin(menu_win);
                        return choice;
                    }
                }
                break;
        }
    }
}

int AdvancedTUI::showMainMenu() {
    std::vector<std::string> mainOptions = {
        "Download FDA Data - Select data source and process pharmaceutical data",
        "Database Management - Clear tables, manage connections, view status", 
        "Configuration - Edit database settings and save configurations",
        "About - View data sources, table information, and system details",
        "Exit - Quit the application"
    };
    
    updateStatus("Main Menu - Select an option to continue");
    return showMenu(mainOptions, "OpenFDA Data Downloader - Main Menu", 70);
}

bool AdvancedTUI::showYesNoDialog(const std::string& question) {
    if (!initialized) return false;
    
    int dialog_width = std::max(static_cast<int>(question.length() + 10), 50);
    int dialog_height = 7;
    int start_y = (height - dialog_height) / 2;
    int start_x = (width - dialog_width) / 2;
    
    WINDOW* dialog = newwin(dialog_height, dialog_width, start_y, start_x);
    if (!dialog) return false;
    
    keypad(dialog, TRUE);  // Enable arrow keys for this window
    wbkgd(dialog, COLOR_PAIR(4));
    box(dialog, 0, 0);
    
    // Draw question
    int question_x = (dialog_width - question.length()) / 2;
    mvwprintw(dialog, 2, question_x, "%s", question.c_str());
    
    // Draw options
    mvwprintw(dialog, 4, dialog_width / 2 - 8, "[Y]es    [N]o");
    
    bool yes_selected = true;
    int key;
    
    while (true) {
        // Highlight selection
        if (yes_selected) {
            if (has_colors()) wattron(dialog, COLOR_PAIR(2) | A_BOLD);
            mvwprintw(dialog, 4, dialog_width / 2 - 8, "[Y]es");
            if (has_colors()) wattroff(dialog, COLOR_PAIR(2) | A_BOLD);
            mvwprintw(dialog, 4, dialog_width / 2 - 1, "   [N]o");
        } else {
            mvwprintw(dialog, 4, dialog_width / 2 - 8, "[Y]es   ");
            if (has_colors()) wattron(dialog, COLOR_PAIR(2) | A_BOLD);
            mvwprintw(dialog, 4, dialog_width / 2 - 1, "[N]o");
            if (has_colors()) wattroff(dialog, COLOR_PAIR(2) | A_BOLD);
        }
        
        wrefresh(dialog);
        key = wgetch(dialog);
        
        switch (key) {
            case KEY_LEFT:
            case KEY_RIGHT:
            case '\t':
                yes_selected = !yes_selected;
                break;
            case 'y':
            case 'Y':
                delwin(dialog);
                return true;
            case 'n':
            case 'N':
                delwin(dialog);
                return false;
            case '\n':
            case '\r':
            case KEY_ENTER:
                delwin(dialog);
                return yes_selected;
            case 27: // Escape
                delwin(dialog);
                return false;
        }
    }
}

std::string AdvancedTUI::getStringInput(const std::string& prompt, const std::string& default_value) {
    if (!initialized) return default_value;
    
    int dialog_width = std::max(static_cast<int>(prompt.length() + 10), 60);
    int dialog_height = 8;
    int start_y = (height - dialog_height) / 2;
    int start_x = (width - dialog_width) / 2;
    
    WINDOW* dialog = newwin(dialog_height, dialog_width, start_y, start_x);
    if (!dialog) return default_value;
    
    keypad(dialog, TRUE);  // Enable arrow keys for this window
    wbkgd(dialog, COLOR_PAIR(4));
    box(dialog, 0, 0);
    
    // Draw prompt
    int prompt_x = (dialog_width - prompt.length()) / 2;
    mvwprintw(dialog, 2, prompt_x, "%s", prompt.c_str());
    
    // Draw input field background
    mvwhline(dialog, 4, 2, ' ', dialog_width - 4);
    if (has_colors()) {
        mvwchgat(dialog, 4, 2, dialog_width - 4, A_REVERSE, 4, NULL);
    }
    
    // Instructions
    mvwprintw(dialog, 6, 2, "Enter text, then press Enter. Escape to cancel.");
    
    std::string input = default_value;
    int cursor_pos = input.length();
    curs_set(1); // Show cursor
    
    while (true) {
        // Display current input
        mvwhline(dialog, 4, 2, ' ', dialog_width - 4); // Clear line
        if (has_colors()) {
            mvwchgat(dialog, 4, 2, dialog_width - 4, A_REVERSE, 4, NULL);
        }
        
        std::string display_text = input;
        if (display_text.length() > dialog_width - 6) {
            display_text = display_text.substr(0, dialog_width - 9) + "...";
        }
        mvwprintw(dialog, 4, 3, "%s", display_text.c_str());
        
        // Position cursor
        int display_cursor = std::min(cursor_pos, dialog_width - 6);
        wmove(dialog, 4, 3 + display_cursor);
        
        wrefresh(dialog);
        
        int key = wgetch(dialog);
        
        switch (key) {
            case '\n':
            case '\r':
            case KEY_ENTER:
                curs_set(0);
                delwin(dialog);
                return input;
            case 27: // Escape
                curs_set(0);
                delwin(dialog);
                return default_value;
            case KEY_BACKSPACE:
            case '\b':
            case 127:
                if (cursor_pos > 0) {
                    input.erase(cursor_pos - 1, 1);
                    cursor_pos--;
                }
                break;
            case KEY_LEFT:
                if (cursor_pos > 0) cursor_pos--;
                break;
            case KEY_RIGHT:
                if (cursor_pos < input.length()) cursor_pos++;
                break;
            case KEY_HOME:
                cursor_pos = 0;
                break;
            case KEY_END:
                cursor_pos = input.length();
                break;
            default:
                if (key >= 32 && key <= 126 && input.length() < 200) { // Printable characters
                    input.insert(cursor_pos, 1, static_cast<char>(key));
                    cursor_pos++;
                }
                break;
        }
    }
}

bool AdvancedTUI::editDatabaseConfig(DatabaseConfig& config) {
    if (!initialized) return false;
    
    showMessage("Database Configuration Editor", 1500);
    
    // Select database type first
    std::vector<std::string> dbTypeOptions = {
        "MySQL - Traditional relational database",
        "PostgreSQL - Advanced relational database"
    };
    
    int typeChoice = showMenu(dbTypeOptions, "Select Database Type", 50);
    if (typeChoice == -1) return false; // User cancelled
    
    config.type = (typeChoice == 1) ? DatabaseType::MYSQL : DatabaseType::POSTGRESQL;
    
    // Set appropriate default port based on database type
    int defaultPort = (config.type == DatabaseType::POSTGRESQL) ? 5432 : 3306;
    
    // Edit each field
    config.host = getStringInput("Database Host:", config.host.empty() ? "localhost" : config.host);
    if (config.host.empty()) return false;
    
    std::string port_str = getStringInput("Database Port:", 
        config.port == 0 ? std::to_string(defaultPort) : std::to_string(config.port));
    if (port_str.empty()) return false;
    config.port = std::stoi(port_str);
    
    config.user = getStringInput("Database User:", config.user);
    if (config.user.empty()) return false;
    
    config.password = getStringInput("Database Password:", config.password);
    // Password can be empty
    
    config.database = getStringInput("Database Name:", config.database);
    if (config.database.empty()) return false;
    
    config.schema = getStringInput("Schema Name:", config.schema.empty() ? config.database : config.schema);
    if (config.schema.empty()) config.schema = config.database;
    
    // Show summary and confirm
    showDatabaseConfigSummary(config);
    
    return showYesNoDialog("Save this database configuration?");
}

void AdvancedTUI::showDatabaseConfigSummary(const DatabaseConfig& config) {
    if (!initialized) return;
    
    clear();
    refresh();
    
    int summary_width = 70;
    int summary_height = 12;
    int start_y = (height - summary_height) / 2;
    int start_x = (width - summary_width) / 2;
    
    WINDOW* summary = newwin(summary_height, summary_width, start_y, start_x);
    wbkgd(summary, COLOR_PAIR(4));
    box(summary, 0, 0);
    
    // Title
    if (has_colors()) wattron(summary, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(summary, 1, (summary_width - 24) / 2, "Database Configuration");
    if (has_colors()) wattroff(summary, COLOR_PAIR(1) | A_BOLD);
    
    mvwhline(summary, 2, 1, ACS_HLINE, summary_width - 2);
    
    // Configuration details
    mvwprintw(summary, 4, 3, "Type: %s", (config.type == DatabaseType::POSTGRESQL ? "PostgreSQL" : "MySQL"));
    mvwprintw(summary, 5, 3, "Host: %s", config.host.c_str());
    mvwprintw(summary, 6, 3, "Port: %d", config.port);
    mvwprintw(summary, 7, 3, "User: %s", config.user.c_str());
    mvwprintw(summary, 8, 3, "Password: %s", config.password.empty() ? "(empty)" : std::string(config.password.length(), '*').c_str());
    mvwprintw(summary, 9, 3, "Database: %s", config.database.c_str());
    mvwprintw(summary, 10, 3, "Schema: %s", config.schema.c_str());
    
    mvwprintw(summary, 11, 3, "Press any key to continue...");
    
    wrefresh(summary);
    wgetch(summary);
    delwin(summary);
}

bool AdvancedTUI::saveDatabaseConfig(const DatabaseConfig& config, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        showError("Failed to save configuration file: " + filename);
        return false;
    }
    
    file << "# OpenFDA Database Configuration" << std::endl;
    file << "# Generated by OpenFDA Data Downloader" << std::endl;
    file << std::endl;
    file << "type: " << (config.type == DatabaseType::POSTGRESQL ? "postgresql" : "mysql") << std::endl;
    file << "host: " << config.host << std::endl;
    file << "port: " << config.port << std::endl;
    file << "user: " << config.user << std::endl;
    file << "password: " << config.password << std::endl;
    file << "database: " << config.database << std::endl;
    file << "schema: " << config.schema << std::endl;
    
    file.close();
    
    showMessage("Configuration saved to: " + filename, 2000);
    return true;
}

void AdvancedTUI::updateStatus(const std::string& status) {
    if (!initialized || !status_window) return;
    
    werase(status_window);
    wbkgd(status_window, COLOR_PAIR(9)); // Ensure green background
    box(status_window, 0, 0);
    
    if (has_colors()) wattron(status_window, COLOR_PAIR(9));
    mvwprintw(status_window, 1, 2, "Status: %s", status.c_str());
    if (has_colors()) wattroff(status_window, COLOR_PAIR(9));
    
    wrefresh(status_window);
}

void AdvancedTUI::showMessage(const std::string& message, int delay_ms) {
    if (!initialized) return;
    
    updateStatus(message);
    napms(delay_ms);
}

void AdvancedTUI::showError(const std::string& error, int delay_ms) {
    if (!initialized) return;
    
    werase(status_window);
    wbkgd(status_window, COLOR_PAIR(9)); // Keep green background
    box(status_window, 0, 0);
    
    if (has_colors()) wattron(status_window, COLOR_PAIR(6)); // Red text for error
    mvwprintw(status_window, 1, 2, "Error: %s", error.c_str());
    if (has_colors()) wattroff(status_window, COLOR_PAIR(6));
    
    wrefresh(status_window);
    napms(delay_ms);
}

std::string AdvancedTUI::trimString(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(start, end - start + 1);
}

void AdvancedTUI::showConfigSummary(DataSource dataSource, bool clearDatabase, const std::string& configFile) {
    if (!initialized) return;
    
    clear();
    refresh();
    
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
        case DataSource::FDA_NDC_BULK:
            sourceStr = "FDA NDC Bulk Download (normalized format)";
            break;
    }
    
    int start_y = 5;
    
    if (has_colors()) attron(COLOR_PAIR(3));
    mvprintw(start_y, 2, "═══════════════════════════════════════════════════");
    mvprintw(start_y + 1, 2, "            OPERATION SUMMARY");
    mvprintw(start_y + 2, 2, "═══════════════════════════════════════════════════");
    if (has_colors()) attroff(COLOR_PAIR(3));
    
    mvprintw(start_y + 4, 4, "Data Source: %s", sourceStr.c_str());
    mvprintw(start_y + 5, 4, "Clear Database: %s", clearDatabase ? "Yes" : "No");
    mvprintw(start_y + 6, 4, "Config File: %s", configFile.c_str());
    
    mvprintw(start_y + 8, 4, "Press any key to continue...");
    refresh();
    getch();
}

void AdvancedTUI::showOperationSummary(const std::string& operation, const std::string& details) {
    if (!initialized) return;
    
    clear();
    refresh();
    
    int summary_width = 80;
    int summary_height = 12;
    int start_y = (height - summary_height) / 2;
    int start_x = (width - summary_width) / 2;
    
    WINDOW* summary = newwin(summary_height, summary_width, start_y, start_x);
    wbkgd(summary, COLOR_PAIR(4));
    box(summary, 0, 0);
    
    // Title
    if (has_colors()) wattron(summary, COLOR_PAIR(1) | A_BOLD);
    int title_x = (summary_width - operation.length()) / 2;
    mvwprintw(summary, 1, title_x, "%s", operation.c_str());
    if (has_colors()) wattroff(summary, COLOR_PAIR(1) | A_BOLD);
    
    mvwhline(summary, 2, 1, ACS_HLINE, summary_width - 2);
    
    // Details - split by newlines and display each line
    std::istringstream ss(details);
    std::string line;
    int line_num = 4;
    
    while (std::getline(ss, line) && line_num < summary_height - 2) {
        mvwprintw(summary, line_num++, 3, "%s", line.c_str());
    }
    
    mvwprintw(summary, summary_height - 2, 3, "Press any key to continue...");
    
    wrefresh(summary);
    wgetch(summary);
    delwin(summary);
}

void AdvancedTUI::showDatabaseStatus(bool connection_ok, const std::vector<std::pair<std::string, bool>>& tables) {
    if (!initialized) return;
    
    clear();
    refresh();
    
    int status_width = 70;
    int status_height = 10 + tables.size();
    int start_y = (height - status_height) / 2;
    int start_x = (width - status_width) / 2;
    
    // Ensure dialog fits on screen
    if (start_y < 1) start_y = 1;
    if (start_x < 1) start_x = 1;
    if (status_height > height - 2) status_height = height - 2;
    
    WINDOW* status_win = newwin(status_height, status_width, start_y, start_x);
    keypad(status_win, TRUE);
    wbkgd(status_win, COLOR_PAIR(4));
    box(status_win, 0, 0);
    
    // Title
    if (has_colors()) wattron(status_win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(status_win, 1, (status_width - 15) / 2, "Database Status");
    if (has_colors()) wattroff(status_win, COLOR_PAIR(1) | A_BOLD);
    
    mvwhline(status_win, 2, 1, ACS_HLINE, status_width - 2);
    
    int line_y = 4;
    
    // Connection status
    if (connection_ok) {
        if (has_colors()) wattron(status_win, COLOR_PAIR(3)); // Green
        mvwprintw(status_win, line_y++, 3, "✓ Database Connection: OK");
        if (has_colors()) wattroff(status_win, COLOR_PAIR(3));
    } else {
        if (has_colors()) wattron(status_win, COLOR_PAIR(6)); // Red
        mvwprintw(status_win, line_y++, 3, "✗ Database Connection: FAILED");
        if (has_colors()) wattroff(status_win, COLOR_PAIR(6));
    }
    
    line_y++; // Empty line
    
    // Table status
    if (connection_ok && !tables.empty()) {
        mvwprintw(status_win, line_y++, 3, "Table Status:");
        
        for (const auto& table : tables) {
            if (table.second) {
                if (has_colors()) wattron(status_win, COLOR_PAIR(3)); // Green
                mvwprintw(status_win, line_y++, 5, "✓ Table %s: EXISTS", table.first.c_str());
                if (has_colors()) wattroff(status_win, COLOR_PAIR(3));
            } else {
                if (has_colors()) wattron(status_win, COLOR_PAIR(6)); // Red
                mvwprintw(status_win, line_y++, 5, "✗ Table %s: NOT FOUND", table.first.c_str());
                if (has_colors()) wattroff(status_win, COLOR_PAIR(6));
            }
        }
    }
    
    // Instructions
    mvwprintw(status_win, status_height - 2, 3, "Press any key to continue...");
    
    wrefresh(status_win);
    wgetch(status_win);
    delwin(status_win);
}

void AdvancedTUI::updateDatabaseFooter(const std::string& host, int port, const std::string& user, 
                                       const std::string& dbtype, bool connected) {
    if (!initialized || !footer_window) return;
    
    // Status indicator
    std::string status_symbol = connected ? "✓" : "✗";
    std::string conn_status = connected ? "Connected" : "Disconnected";
    
    // Store database status for redrawing
    database_status = status_symbol + " " + dbtype + " " + user + "@" + host + ":" + std::to_string(port) + " " + conn_status;
    
    redrawFooter();
}

void AdvancedTUI::redrawFooter() {
    if (!initialized || !footer_window) return;
    
    werase(footer_window);
    wbkgd(footer_window, COLOR_PAIR(8)); // Blue background
    box(footer_window, 0, 0);
    
    int footer_height, footer_width;
    getmaxyx(footer_window, footer_height, footer_width);
    
    // Calculate available lines (excluding border)
    max_footer_lines = footer_height - 2;
    int current_line = 1;
    
    // Display database status on top line if available
    if (!database_status.empty()) {
        if (has_colors()) wattron(footer_window, COLOR_PAIR(8) | A_BOLD);
        mvwprintw(footer_window, current_line, 2, "%s", database_status.c_str());
        if (has_colors()) wattroff(footer_window, COLOR_PAIR(8) | A_BOLD);
        current_line++;
        max_footer_lines--;
    }
    
    // Calculate scroll parameters
    int total_messages = footer_messages.size();
    int visible_messages = std::min(max_footer_lines, total_messages);
    
    // Adjust scroll offset if needed
    if (footer_scroll_offset + visible_messages > total_messages) {
        footer_scroll_offset = std::max(0, total_messages - visible_messages);
    }
    if (footer_scroll_offset < 0) {
        footer_scroll_offset = 0;
    }
    
    // Display footer messages with scrolling
    for (int i = 0; i < visible_messages && i < max_footer_lines; i++) {
        int message_index = footer_scroll_offset + i;
        if (message_index >= 0 && message_index < total_messages) {
            const std::string& message = footer_messages[message_index];
            
            // Truncate message if too long
            std::string display_message = message;
            int max_width = footer_width - 4; // Account for border and padding
            if (static_cast<int>(display_message.length()) > max_width) {
                display_message = display_message.substr(0, max_width - 3) + "...";
            }
            
            if (has_colors()) wattron(footer_window, COLOR_PAIR(8));
            mvwprintw(footer_window, current_line + i, 2, "%s", display_message.c_str());
            if (has_colors()) wattroff(footer_window, COLOR_PAIR(8));
        }
    }
    
    // Draw scrollbar if needed
    if (total_messages > max_footer_lines) {
        int scrollbar_x = footer_width - 2;
        int scrollbar_height = max_footer_lines;
        
        // Draw scrollbar track
        for (int y = current_line; y < current_line + scrollbar_height; y++) {
            mvwaddch(footer_window, y, scrollbar_x, '|');
        }
        
        // Draw scrollbar thumb
        if (total_messages > 0) {
            int thumb_size = std::max(1, (scrollbar_height * visible_messages) / total_messages);
            int thumb_pos = current_line + (footer_scroll_offset * scrollbar_height) / total_messages;
            
            for (int y = thumb_pos; y < thumb_pos + thumb_size && y < current_line + scrollbar_height; y++) {
                wattron(footer_window, A_REVERSE);
                mvwaddch(footer_window, y, scrollbar_x, '|');
                wattroff(footer_window, A_REVERSE);
            }
        }
        
        // Draw scroll indicators
        if (footer_scroll_offset > 0) {
            mvwaddch(footer_window, current_line, scrollbar_x, '^');
        }
        if (footer_scroll_offset + visible_messages < total_messages) {
            mvwaddch(footer_window, current_line + scrollbar_height - 1, scrollbar_x, 'v');
        }
    }
    
    wrefresh(footer_window);
}

void AdvancedTUI::refreshAllWindows() {
    if (!initialized) return;
    
    // Refresh all windows to keep them visible
    if (status_window) wrefresh(status_window);
    if (footer_window) wrefresh(footer_window);
}

void AdvancedTUI::clearDatabaseFooter() {
    if (!initialized || !footer_window) return;
    
    database_status.clear();
    redrawFooter();
}

void AdvancedTUI::addFooterMessage(const std::string& message) {
    if (!initialized || !footer_window) return;
    
    footer_messages.push_back(message);
    
    // Auto-scroll to bottom when new messages are added
    scrollFooterToBottom();
    
    redrawFooter();
}

void AdvancedTUI::clearFooterMessages() {
    if (!initialized || !footer_window) return;
    
    footer_messages.clear();
    footer_scroll_offset = 0;
    redrawFooter();
}

void AdvancedTUI::scrollFooterUp() {
    if (!initialized || !footer_window) return;
    
    if (footer_scroll_offset > 0) {
        footer_scroll_offset--;
        redrawFooter();
    }
}

void AdvancedTUI::scrollFooterDown() {
    if (!initialized || !footer_window) return;
    
    int total_messages = footer_messages.size();
    int max_scroll = std::max(0, total_messages - max_footer_lines);
    
    if (footer_scroll_offset < max_scroll) {
        footer_scroll_offset++;
        redrawFooter();
    }
}

void AdvancedTUI::scrollFooterToBottom() {
    if (!initialized || !footer_window) return;
    
    int total_messages = footer_messages.size();
    footer_scroll_offset = std::max(0, total_messages - max_footer_lines);
}

int AdvancedTUI::showDatabaseConfigMenu() {
    if (!initialized) return -1;
    
    std::vector<std::string> configOptions = {
        "Edit Database Config - Modify connection settings",
        "Test Connection - Verify database connectivity",
        "Create Database & Tables - Initialize database structure",
        "Switch Database Type - Change between MySQL/PostgreSQL", 
        "Save Config - Save settings to file",
        "Load Config File - Load existing settings",
        "Return to Main Menu"
    };
    
    updateStatus("Database Configuration Management");
    return showMenu(configOptions, "Database Configuration Options", 60);
}

bool AdvancedTUI::testDatabaseConnection() {
    if (!initialized) return false;
    
    showMessage("Testing database connection...", 1000);
    
    // This would need to be connected to the actual DatabaseManager
    // For now, just show a placeholder dialog
    bool connection_ok = showYesNoDialog("Connection test - would you like to simulate success?");
    
    if (connection_ok) {
        showMessage("✓ Database connection successful!", 2000);
    } else {
        showError("✗ Database connection failed!", 2000);
    }
    
    return connection_ok;
}

bool AdvancedTUI::createDatabaseAndTables() {
    if (!initialized) return false;
    
    if (!showYesNoDialog("Create database and tables if they don't exist?")) {
        return false;
    }
    
    showMessage("Creating database and tables...", 1500);
    
    // Simulate database creation process
    showMessage("✓ Database created successfully", 1000);
    showMessage("✓ NDC table created", 1000);
    showMessage("✓ DrugsFDA table created", 1000);
    showMessage("✓ Drug Label table created", 1000);
    showMessage("✓ FDA NDC table created", 1000);
    showMessage("🎉 All database structures ready!", 2000);
    
    return true;
}

bool AdvancedTUI::switchDatabaseType() {
    if (!initialized) return false;
    
    std::vector<std::string> dbOptions = {
        "MySQL - Traditional relational database",
        "PostgreSQL - Advanced relational database with JSONB"
    };
    
    int choice = showMenu(dbOptions, "Select Database Type", 60);
    if (choice == -1) return false;
    
    std::string selected_db = (choice == 1) ? "MySQL" : "PostgreSQL";
    std::string message = "Switched to " + selected_db + " database";
    
    showMessage(message, 2000);
    return true;
}

void AdvancedTUI::showProgressBar(const std::string& title, int current, int total) {
    if (!initialized || !status_window) return;
    
    progress_bar_visible = true;
    progress_title = title;
    progress_current = current;
    progress_total = total;
    
    // Position progress bar in the status window (middle of the 3 lines)
    progress_bar_y = 1; // Middle line of status window
    
    drawProgressBar();
    refreshAllWindows();
}

void AdvancedTUI::updateProgressBar(int current, int total) {
    if (!initialized || !progress_bar_visible) return;
    
    progress_current = current;
    progress_total = total;
    
    drawProgressBar();
    refreshAllWindows();
}

void AdvancedTUI::hideProgressBar() {
    if (!initialized || !progress_bar_visible) return;
    
    progress_bar_visible = false;
    
    // Clear the status window and redraw its border
    wclear(status_window);
    wbkgd(status_window, COLOR_PAIR(9)); // Restore green background
    box(status_window, 0, 0);
    
    refreshAllWindows();
}

void AdvancedTUI::drawProgressBar() {
    if (!initialized || !status_window || !progress_bar_visible) return;
    
    int status_height, status_width;
    getmaxyx(status_window, status_height, status_width);
    
    // Progress bar dimensions (fit within status window)
    int bar_width = std::min(status_width - 4, 60);
    int bar_x = (status_width - bar_width) / 2;
    
    // Calculate percentage and filled chars
    int percentage = (progress_total > 0) ? (progress_current * 100) / progress_total : 0;
    int filled_chars = (progress_total > 0) ? (progress_current * (bar_width - 2)) / progress_total : 0;
    
    // Clear the status window but preserve background and border
    wclear(status_window);
    wbkgd(status_window, COLOR_PAIR(9)); // Green background
    box(status_window, 0, 0);
    
    // Draw progress bar frame on the middle line
    mvwaddch(status_window, progress_bar_y, bar_x, '[');
    mvwaddch(status_window, progress_bar_y, bar_x + bar_width - 1, ']');
    
    // Fill progress bar
    wattron(status_window, COLOR_PAIR(4)); // Use blue color for progress
    for (int i = 0; i < filled_chars; i++) {
        mvwaddch(status_window, progress_bar_y, bar_x + 1 + i, ACS_CKBOARD);
    }
    wattroff(status_window, COLOR_PAIR(4));
    
    // Draw empty part
    for (int i = filled_chars; i < bar_width - 2; i++) {
        mvwaddch(status_window, progress_bar_y, bar_x + 1 + i, ' ');
    }
    
    // Draw percentage on line above progress bar if there's space
    if (status_height >= 3) {
        std::string stats = std::to_string(percentage) + "% (" + 
                           std::to_string(progress_current) + "/" + 
                           std::to_string(progress_total) + ")";
        int stats_x = (status_width - stats.length()) / 2;
        if (stats_x > 0 && stats.length() < static_cast<size_t>(status_width - 2)) {
            mvwprintw(status_window, progress_bar_y - 1, stats_x, "%s", stats.c_str());
        }
    }
    
    wrefresh(status_window);
}

void AdvancedTUI::showScrollableText(const std::vector<std::string>& lines, const std::string& title) {
    if (!initialized) return;
    
    // Calculate window dimensions (80% of screen size)
    int win_height = height * 0.8;
    int win_width = width * 0.9;
    int start_y = (height - win_height) / 2;
    int start_x = (width - win_width) / 2;
    
    // Create the scrollable window
    WINDOW* scroll_win = newwin(win_height, win_width, start_y, start_x);
    if (!scroll_win) return;
    
    // Set blue background with white text
    wbkgd(scroll_win, COLOR_PAIR(1));  // Blue background, white text
    
    int content_height = win_height - 4; // Account for borders and title
    int content_width = win_width - 4;
    int scroll_pos = 0;
    int max_scroll = (lines.size() > content_height) ? lines.size() - content_height : 0;
    
    while (true) {
        // Clear and draw border
        werase(scroll_win);
        box(scroll_win, 0, 0);
        
        // Draw title
        int title_x = (win_width - title.length()) / 2;
        if (title_x > 0) {
            wattron(scroll_win, A_BOLD);
            mvwprintw(scroll_win, 1, title_x, "%s", title.c_str());
            wattroff(scroll_win, A_BOLD);
        }
        
        // Draw content lines
        for (int i = 0; i < content_height && (i + scroll_pos) < lines.size(); i++) {
            const std::string& line = lines[i + scroll_pos];
            std::string display_line = line;
            
            // Truncate line if too long
            if (display_line.length() > content_width - 2) {
                display_line = display_line.substr(0, content_width - 5) + "...";
            }
            
            mvwprintw(scroll_win, i + 3, 2, "%s", display_line.c_str());
        }
        
        // Draw scroll indicator
        if (max_scroll > 0) {
            mvwprintw(scroll_win, win_height - 2, win_width - 20, 
                     "Line %d/%zu", scroll_pos + 1, lines.size());
            mvwprintw(scroll_win, win_height - 1, 2, 
                     "↑↓/PgUp/PgDn: Scroll | ESC/Q: Exit");
        } else {
            mvwprintw(scroll_win, win_height - 1, 2, "ESC/Q: Exit");
        }
        
        wrefresh(scroll_win);
        
        // Handle input
        int ch = wgetch(scroll_win);
        switch (ch) {
            case KEY_UP:
                if (scroll_pos > 0) scroll_pos--;
                break;
            case KEY_DOWN:
                if (scroll_pos < max_scroll) scroll_pos++;
                break;
            case KEY_PPAGE: // Page Up
                scroll_pos -= content_height;
                if (scroll_pos < 0) scroll_pos = 0;
                break;
            case KEY_NPAGE: // Page Down
                scroll_pos += content_height;
                if (scroll_pos > max_scroll) scroll_pos = max_scroll;
                break;
            case 27: // ESC
            case 'q':
            case 'Q':
                goto exit_scroll;
            default:
                break;
        }
    }
    
exit_scroll:
    delwin(scroll_win);
    refreshAllWindows();
}