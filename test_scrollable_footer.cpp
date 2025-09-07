#include "include/tui.h"
#include <unistd.h>

int main() {
    AdvancedTUI tui;
    
    if (!tui.initialize()) {
        return 1;
    }
    
    // Add many messages to test scrolling
    for (int i = 1; i <= 20; i++) {
        tui.addFooterMessage("Message " + std::to_string(i) + " - This is a test message to demonstrate scrolling functionality");
        usleep(200000); // 0.2 second delay
    }
    
    // Keep the display open for viewing
    sleep(5);
    
    tui.cleanup();
    return 0;
}