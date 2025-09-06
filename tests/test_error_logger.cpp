#include "error_logger.h"
#include <iostream>

int main() {
    std::cout << "🧪 Testing Error Logger Functionality" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Create error logger
    ErrorLogger logger("./test_logs/");
    
    // Create some sample JSON records
    Json::Value sample_record1;
    sample_record1["product_ndc"] = "12345-678-90";
    sample_record1["brand_name"] = "Test Drug A";
    sample_record1["generic_name"] = "testamine";
    
    Json::Value sample_record2;
    sample_record2["product_ndc"] = "98765-432-10";
    sample_record2["brand_name"] = "Test Drug B";
    sample_record2["generic_name"] = "testanol";
    
    // Test different types of errors
    std::cout << "\n📝 Logging sample errors..." << std::endl;
    
    // Log some duplicates
    logger.logDuplicate("12345-678-90", "ndc_data", sample_record1, "test-file-part1.json", 15);
    logger.logDuplicate("12345-678-90", "ndc_data", sample_record1, "test-file-part1.json", 234);
    logger.logDuplicate("98765-432-10", "fda_ndc_data", sample_record2, "test-file-part2.json", 67);
    
    // Log some processing errors
    logger.logProcessingError("Invalid NDC format detected", 89, sample_record1);
    logger.logDatabaseError("Connection timeout during insertion", sample_record2);
    logger.logJSONError("Malformed JSON structure in file", "corrupted-file.json");
    logger.logNetworkError("Failed to download bulk file", "https://example.com/data.zip");
    
    // Log more duplicates to test grouping
    for (int i = 0; i < 5; i++) {
        Json::Value temp_record;
        temp_record["product_ndc"] = "11111-111-" + std::to_string(i);
        temp_record["brand_name"] = "Duplicate Drug " + std::to_string(i);
        
        logger.logDuplicate("11111-111-" + std::to_string(i), "drug_label_data", 
                           temp_record, "batch-file.json", i * 100);
    }
    
    std::cout << "✅ Sample errors logged" << std::endl;
    
    // Print summary
    std::cout << "\n📊 Current Status:" << std::endl;
    std::cout << "Total Errors: " << logger.getErrorCount() << std::endl;
    logger.printSummary();
    
    // Test manual file writing
    std::cout << "\n💾 Testing manual file export..." << std::endl;
    logger.writeErrorsToFile("./test_logs/manual_errors_test.json");
    logger.writeDuplicatesToFile("./test_logs/manual_duplicates_test.json");
    
    // Test summary JSON
    Json::Value summary = logger.generateSummary();
    std::cout << "\n📋 JSON Summary Generated:" << std::endl;
    std::cout << "Total Errors: " << summary["total_errors"].asInt() << std::endl;
    std::cout << "Total Duplicates: " << summary["total_duplicates"].asInt() << std::endl;
    
    std::cout << "\n✅ Error Logger Test Completed!" << std::endl;
    std::cout << "Check the ./test_logs/ directory for generated JSON files." << std::endl;
    
    return 0;
}