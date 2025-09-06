#include "tui.h"
#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>

AdvancedTUI::AdvancedTUI() : main_window(nullptr), status_window(nullptr), initialized(false) {}

AdvancedTUI::~AdvancedTUI() {
    cleanup();
}

bool AdvancedTUI::initialize() {
    // Initialize ncurses
    initscr();
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
    if (height < 20 || width < 100) {
        endwin();
        std::cerr << "Terminal too small. Minimum size: 100x20" << std::endl;
        return false;
    }
    
    // Initialize colors
    initColors();
    
    // Create main window (leave space for status bar)
    main_window = newwin(height - 3, width, 0, 0);
    status_window = newwin(3, width, height - 3, 0);
    
    if (!main_window || !status_window) {
        cleanup();
        return false;
    }
    
    // Set up status window
    wbkgd(status_window, COLOR_PAIR(4));
    box(status_window, 0, 0);
    
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
    }
}

void AdvancedTUI::cleanup() {
    if (initialized) {
        if (main_window) delwin(main_window);
        if (status_window) delwin(status_window);
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
    
    clear();
    refresh();
    
    int menu_width = getMenuWidth(options, title, min_width);
    int menu_height = options.size() + 6;
    int start_y = (height - menu_height) / 2;
    int start_x = (width - menu_width) / 2;
    
    // Ensure menu fits on screen
    if (start_y < 0) start_y = 1;
    if (start_x < 0) start_x = 1;
    if (menu_width > width - 2) menu_width = width - 2;
    
    WINDOW* menu_win = newwin(menu_height, menu_width, start_y, start_x);
    if (!menu_win) return -1;
    
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
        "1. Download FDA Data - Select data source and process pharmaceutical data",
        "2. Database Management - Clear tables, manage connections, view status",
        "3. Configuration - Edit database settings and save configurations",
        "4. Exit - Quit the application"
    };
    
    updateStatus("Main Menu - Select an option to continue");
    return showMenu(mainOptions, "OpenFDA Data Downloader - Main Menu", 90);
}

bool AdvancedTUI::showYesNoDialog(const std::string& question) {
    if (!initialized) return false;
    
    int dialog_width = std::max(static_cast<int>(question.length() + 10), 50);
    int dialog_height = 7;
    int start_y = (height - dialog_height) / 2;
    int start_x = (width - dialog_width) / 2;
    
    WINDOW* dialog = newwin(dialog_height, dialog_width, start_y, start_x);
    if (!dialog) return false;
    
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
    
    // Edit each field
    config.host = getStringInput("Database Host:", config.host.empty() ? "localhost" : config.host);
    if (config.host.empty()) return false;
    
    std::string port_str = getStringInput("Database Port:", 
        config.port == 0 ? "3306" : std::to_string(config.port));
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
    mvwprintw(summary, 4, 3, "Host: %s", config.host.c_str());
    mvwprintw(summary, 5, 3, "Port: %d", config.port);
    mvwprintw(summary, 6, 3, "User: %s", config.user.c_str());
    mvwprintw(summary, 7, 3, "Password: %s", config.password.empty() ? "(empty)" : std::string(config.password.length(), '*').c_str());
    mvwprintw(summary, 8, 3, "Database: %s", config.database.c_str());
    mvwprintw(summary, 9, 3, "Schema: %s", config.schema.c_str());
    
    mvwprintw(summary, 10, 3, "Press any key to continue...");
    
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
    box(status_window, 0, 0);
    mvwprintw(status_window, 1, 2, "Status: %s", status.c_str());
    wrefresh(status_window);
}

void AdvancedTUI::showMessage(const std::string& message, int delay_ms) {
    if (!initialized) return;
    
    updateStatus(message);
    napms(delay_ms);
}

void AdvancedTUI::showError(const std::string& error, int delay_ms) {
    if (!initialized) return;
    
    if (has_colors()) wattron(status_window, COLOR_PAIR(6));
    werase(status_window);
    box(status_window, 0, 0);
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