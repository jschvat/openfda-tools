#include "include/tui.h"
#include <unistd.h>

int main() {
    AdvancedTUI tui;
    
    if (!tui.initialize()) {
        return 1;
    }
    
    // Test initial state
    tui.clearDatabaseFooter();
    sleep(2);
    
    // Test MySQL connection
    tui.updateDatabaseFooter("miniserver.home", 3306, "root", "MySQL", true);
    sleep(2);
    
    // Test PostgreSQL connection
    tui.updateDatabaseFooter("miniserver.local", 5432, "jason", "PostgreSQL", true);
    sleep(2);
    
    // Test disconnected state
    tui.updateDatabaseFooter("miniserver.local", 5432, "jason", "PostgreSQL", false);
    sleep(2);
    
    tui.cleanup();
    return 0;
}