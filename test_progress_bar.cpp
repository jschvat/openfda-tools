#include "include/tui.h"
#include <unistd.h>

int main() {
    AdvancedTUI tui;
    
    if (!tui.initialize()) {
        return 1;
    }
    
    // Test progress bar functionality
    tui.showProgressBar("Processing Data", 0, 100);
    sleep(1);
    
    // Simulate progress updates
    for (int i = 0; i <= 100; i += 10) {
        tui.updateProgressBar(i, 100);
        usleep(500000); // 0.5 second delay
    }
    
    sleep(1);
    
    // Test with different total
    tui.showProgressBar("Downloading Files", 0, 50);
    sleep(1);
    
    for (int i = 0; i <= 50; i += 5) {
        tui.updateProgressBar(i, 50);
        usleep(300000); // 0.3 second delay
    }
    
    sleep(1);
    tui.hideProgressBar();
    sleep(1);
    
    tui.cleanup();
    return 0;
}