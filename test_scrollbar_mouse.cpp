#include "tui.h"
#include <iostream>
#include <vector>

int main() {
    AdvancedTUI tui;
    
    if (!tui.initialize()) {
        std::cerr << "Failed to initialize TUI" << std::endl;
        return 1;
    }
    
    // Create longer test content to ensure scrolling is needed
    std::vector<std::string> testContent;
    
    testContent.push_back("Mouse Scrollbar Test");
    testContent.push_back("==================");
    testContent.push_back("");
    testContent.push_back("This test has enough content to require scrolling.");
    testContent.push_back("The scrollbar should be visible on the right side.");
    testContent.push_back("");
    testContent.push_back("Try these interactions:");
    testContent.push_back("1. Click on the up arrow at the top of scrollbar");
    testContent.push_back("2. Click on the down arrow at the bottom");
    testContent.push_back("3. Click and drag the thumb (solid block)");
    testContent.push_back("4. Click on the track to jump to position");
    testContent.push_back("5. Use mouse wheel to scroll");
    testContent.push_back("");
    
    // Add many more lines to force scrolling
    for (int i = 1; i <= 50; i++) {
        testContent.push_back("Content Line " + std::to_string(i) + 
                             " - This line should be long enough to test horizontal scrolling too.");
    }
    
    testContent.push_back("");
    testContent.push_back("End of test content - you should be able to scroll back to top");
    
    tui.showScrollableText(testContent, "Mouse Scrollbar Test Window");
    
    tui.cleanup();
    return 0;
}