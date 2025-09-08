#include "tui.h"
#include <iostream>
#include <vector>

int main() {
    AdvancedTUI tui;
    
    if (!tui.initialize()) {
        std::cerr << "Failed to initialize TUI" << std::endl;
        return 1;
    }
    
    // Create a simple test content
    std::vector<std::string> testContent = {
        "Test About Section",
        "═══════════════════",
        "",
        "This is a test of the scrollable text functionality.",
        "Use the arrow keys to scroll up and down.",
        "Use Page Up/Page Down for faster scrolling.",
        "Use Home/End to jump to top/bottom.",
        "Mouse wheel should also work for scrolling.",
        "",
        "Line 10",
        "Line 11", 
        "Line 12",
        "Line 13",
        "Line 14",
        "Line 15",
        "Line 16",
        "Line 17",
        "Line 18",
        "Line 19",
        "Line 20",
        "Line 21",
        "Line 22",
        "Line 23",
        "Line 24",
        "Line 25",
        "Line 26",
        "Line 27",
        "Line 28",
        "Line 29",
        "Line 30 - This is the last line"
    };
    
    tui.showScrollableText(testContent, "🧪 Test Scrollable Text Window");
    
    tui.cleanup();
    return 0;
}