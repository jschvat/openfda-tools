#include <ncurses.h>
#include <vector>
#include <string>

int showMenu(const std::vector<std::string>& options, const std::string& title) {
    int height, width;
    getmaxyx(stdscr, height, width);
    
    // Calculate menu window dimensions
    int menu_height = options.size() + 4;
    int menu_width = 60;
    int start_y = 5;
    int start_x = (width - menu_width) / 2;
    
    // Create menu window
    WINDOW* menu_win = newwin(menu_height, menu_width, start_y, start_x);
    if (!menu_win) return -1;
    
    keypad(menu_win, TRUE);  // Enable arrow keys for menu window - THIS IS THE FIX
    box(menu_win, 0, 0);
    
    // Draw title
    mvwprintw(menu_win, 1, 2, "%s", title.c_str());
    mvwhline(menu_win, 2, 1, '-', menu_width - 2);
    
    // Draw options
    for (size_t i = 0; i < options.size(); i++) {
        mvwprintw(menu_win, 3 + i, 2, "%zu. %s", i + 1, options[i].c_str());
    }
    
    wrefresh(menu_win);
    
    // Handle input
    int selected = 0;
    int ch;
    
    while ((ch = wgetch(menu_win)) != 'q') {  // FIXED: using wgetch(menu_win) instead of getch()
        // Clear previous highlight
        mvwprintw(menu_win, 3 + selected, 2, "%d. %s", selected + 1, options[selected].c_str());
        
        switch (ch) {
            case KEY_UP:
                selected = (selected - 1 + options.size()) % options.size();
                break;
            case KEY_DOWN:
                selected = (selected + 1) % options.size();
                break;
            case '\n':
            case '\r':
                delwin(menu_win);
                return selected + 1;
            case '1': case '2': case '3': case '4': case '5':
                if (ch - '0' <= static_cast<int>(options.size())) {
                    delwin(menu_win);
                    return ch - '0';
                }
                break;
        }
        
        // Highlight current selection
        wattron(menu_win, A_REVERSE);
        mvwprintw(menu_win, 3 + selected, 2, "%d. %s", selected + 1, options[selected].c_str());
        wattroff(menu_win, A_REVERSE);
        
        wrefresh(menu_win);
    }
    
    delwin(menu_win);
    return -1;
}

int main() {
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    
    std::vector<std::string> options = {
        "Option 1 - Test arrow keys",
        "Option 2 - Test selection", 
        "Option 3 - Test navigation",
        "Exit"
    };
    
    int choice = showMenu(options, "Test Menu - Arrow Keys Fixed");
    
    endwin();
    
    if (choice == -1) {
        printf("Menu cancelled\n");
    } else {
        printf("Selected option: %d\n", choice);
    }
    
    return 0;
}