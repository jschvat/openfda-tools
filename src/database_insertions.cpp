#include "database.h"
#include <iostream>
#include <sstream>
#include <memory>

// Data Insertion Methods

bool DatabaseManager::insertNDCRecord(const Json::Value& record) {
    std::stringstream query;
    query << "INSERT INTO ndc_data (";
    query << "product_ndc, generic_name, brand_name, labeler_name, product_type, ";
    query << "dosage_form, route, marketing_start_date, marketing_end_date, ";
    query << "product_id, application_number, brand_name_base, brand_name_suffix, ";
    query << "active_ingredients, packaging, openfda_data";
    query << ") VALUES (";
    
    query << escapeString(record.get("product_ndc", "").asString()) << ", ";
    query << escapeString(record.get("generic_name", "").asString()) << ", ";
    query << escapeString(record.get("brand_name", "").asString()) << ", ";
    query << escapeString(record.get("labeler_name", "").asString()) << ", ";
    query << escapeString(record.get("product_type", "").asString()) << ", ";
    query << escapeString(record.get("dosage_form", "").asString()) << ", ";
    
    if (record.isMember("route") && record["route"].isArray() && !record["route"].empty()) {
        query << escapeString(record["route"][0].asString()) << ", ";
    } else {
        query << "NULL, ";
    }
    
    std::string marketing_start = record.get("marketing_start_date", "").asString();
    if (!marketing_start.empty() && marketing_start.length() >= 8) {
        std::string formatted_date = marketing_start.substr(0, 4) + "-" + 
                                   marketing_start.substr(4, 2) + "-" + 
                                   marketing_start.substr(6, 2);
        query << escapeString(formatted_date) << ", ";
    } else {
        query << "NULL, ";
    }
    
    std::string marketing_end = record.get("marketing_end_date", "").asString();
    if (!marketing_end.empty() && marketing_end.length() >= 8) {
        std::string formatted_date = marketing_end.substr(0, 4) + "-" + 
                                   marketing_end.substr(4, 2) + "-" + 
                                   marketing_end.substr(6, 2);
        query << escapeString(formatted_date) << ", ";
    } else {
        query << "NULL, ";
    }
    
    query << escapeString(record.get("product_id", "").asString()) << ", ";
    query << escapeString(record.get("application_number", "").asString()) << ", ";
    query << escapeString(record.get("brand_name_base", "").asString()) << ", ";
    query << escapeString(record.get("brand_name_suffix", "").asString()) << ", ";
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    
    if (record.isMember("active_ingredients")) {
        std::ostringstream stream;
        writer->write(record["active_ingredients"], &stream);
        query << escapeString(stream.str()) << ", ";
    } else {
        query << "NULL, ";
    }
    
    if (record.isMember("packaging")) {
        std::ostringstream stream;
        writer->write(record["packaging"], &stream);
        query << escapeString(stream.str()) << ", ";
    } else {
        query << "NULL, ";
    }
    
    if (record.isMember("openfda")) {
        std::ostringstream stream;
        writer->write(record["openfda"], &stream);
        query << escapeString(stream.str());
    } else {
        query << "NULL";
    }
    
    query << ")";
    
    if (!db_conn->executeQuery(query.str())) {
        std::string error_msg = db_conn->getLastError();
        if (isDuplicateKeyError(error_msg)) {
            std::cout << "ℹ️  Record already exists (product_ndc: " 
                     << record.get("product_ndc", "").asString() << ")" << std::endl;
            return true; // Consider duplicate as success to continue processing
        } else {
            std::cerr << "Error inserting record: " << error_msg << std::endl;
            return false;
        }
    }
    
    return true;
}

bool DatabaseManager::insertDrugsFDARecord(const Json::Value& record) {
    std::stringstream query;
    query << "INSERT INTO drugsfda_data (";
    query << "application_number, sponsor_name, submissions, products, openfda_data";
    query << ") VALUES (";
    
    query << escapeString(record.get("application_number", "").asString()) << ", ";
    query << escapeString(record.get("sponsor_name", "").asString()) << ", ";
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    
    if (record.isMember("submissions")) {
        std::ostringstream stream;
        writer->write(record["submissions"], &stream);
        query << escapeString(stream.str()) << ", ";
    } else {
        query << "NULL, ";
    }
    
    if (record.isMember("products")) {
        std::ostringstream stream;
        writer->write(record["products"], &stream);
        query << escapeString(stream.str()) << ", ";
    } else {
        query << "NULL, ";
    }
    
    if (record.isMember("openfda")) {
        std::ostringstream stream;
        writer->write(record["openfda"], &stream);
        query << escapeString(stream.str());
    } else {
        query << "NULL";
    }
    
    query << ")";
    
    if (!db_conn->executeQuery(query.str())) {
        std::cerr << "Error inserting drugsfda record: " << db_conn->getLastError() << std::endl;
        return false;
    }
    
    return true;
}

bool DatabaseManager::insertDrugLabelRecord(const Json::Value& record) {
    // Helper to safely get first array element
    auto getFirstElement = [this](const Json::Value& parent, const std::string& key) -> std::string {
        if (parent.isMember(key) && parent[key].isArray() && !parent[key].empty()) {
            return parent[key][0].asString();
        }
        return "";
    };
    
    // Helper to format date or return NULL
    auto formatDateOrNull = [this](const std::string& dateStr) -> std::string {
        std::string formatted = formatDate(dateStr);
        return formatted.empty() ? "NULL" : escapeString(formatted);
    };
    
    std::stringstream query;
    query << "INSERT INTO drug_label_data (";
    query << "set_id, effective_time, version, id_number, spl_product_data_elements, ";
    query << "product_ndc, generic_name, brand_name, brand_name_base, brand_name_suffix, ";
    query << "labeler_name, substance_name, active_ingredient, finished, packaging, ";
    query << "listing_expiration_date, openfda_application_number, openfda_brand_name, ";
    query << "openfda_generic_name, openfda_manufacturer_name, openfda_product_ndc, ";
    query << "openfda_product_type, openfda_route, openfda_substance_name, openfda_rxcui, ";
    query << "openfda_spl_id, openfda_spl_set_id, openfda_package_ndc, openfda_nui, ";
    query << "openfda_pharm_class_moa, openfda_pharm_class_cs, openfda_pharm_class_pe, ";
    query << "openfda_pharm_class_epc, openfda_unii, purpose, indications_and_usage, ";
    query << "contraindications, description, clinical_pharmacology, warnings, precautions, ";
    query << "adverse_reactions, drug_interactions, dosage_and_administration, overdosage, ";
    query << "clinical_studies, how_supplied, storage_and_handling, information_for_patients, ";
    query << "warnings_and_cautions, pregnancy, pediatric_use, geriatric_use, nursing_mothers, ";
    query << "carcinogenesis_and_mutagenesis_and_impairment_of_fertility";
    query << ") VALUES (";
    
    // Basic fields
    query << escapeString(record.get("set_id", "").asString()) << ", ";
    query << formatDateOrNull(record.get("effective_time", "").asString()) << ", ";
    query << escapeString(record.get("version", "").asString()) << ", ";
    query << escapeString(record.get("id", "").asString()) << ", ";
    query << escapeString(getArrayAsString(record["spl_product_data_elements"])) << ", ";
    
    // Product and OpenFDA fields
    const Json::Value& openfda = record["openfda"];
    query << escapeString(getFirstElement(openfda, "product_ndc")) << ", ";
    query << escapeString(getArrayAsString(openfda["generic_name"])) << ", ";
    query << escapeString(getArrayAsString(openfda["brand_name"])) << ", ";
    query << escapeString(getArrayAsString(openfda["brand_name_base"])) << ", ";
    query << escapeString(getArrayAsString(openfda["brand_name_suffix"])) << ", ";
    query << escapeString(getArrayAsString(openfda["manufacturer_name"])) << ", ";
    query << escapeString(getArrayAsString(openfda["substance_name"])) << ", ";
    query << escapeString(getArrayAsString(record["active_ingredient"])) << ", ";
    query << escapeString(getArrayAsString(record["finished"])) << ", ";
    query << escapeString(getArrayAsString(record["packaging"])) << ", ";
    query << formatDateOrNull(getArrayAsString(record["listing_expiration_date"])) << ", ";
    
    // More OpenFDA fields (using array of field names to reduce repetition)
    std::vector<std::string> openfda_fields = {
        "application_number", "brand_name", "generic_name", "manufacturer_name", 
        "product_ndc", "product_type", "route", "substance_name", "rxcui",
        "spl_id", "spl_set_id", "package_ndc", "nui", "pharm_class_moa",
        "pharm_class_cs", "pharm_class_pe", "pharm_class_epc", "unii"
    };
    
    for (const auto& field : openfda_fields) {
        query << escapeString(getArrayAsString(openfda[field])) << ", ";
    }
    
    // Label content fields
    std::vector<std::string> label_fields = {
        "purpose", "indications_and_usage", "contraindications", "description",
        "clinical_pharmacology", "warnings", "precautions", "adverse_reactions",
        "drug_interactions", "dosage_and_administration", "overdosage",
        "clinical_studies", "how_supplied", "storage_and_handling",
        "information_for_patients", "warnings_and_cautions", "pregnancy",
        "pediatric_use", "geriatric_use", "nursing_mothers"
    };
    
    for (size_t i = 0; i < label_fields.size(); i++) {
        std::string field_content = getArrayAsString(record[label_fields[i]]);
        
        // Debug: Log field sizes for problematic fields
        if (field_content.length() > 50000) {  // Only log very large fields
            std::cout << "⚠️  Large field detected: " << label_fields[i] 
                      << " (" << field_content.length() << " bytes)" << std::endl;
        }
        
        query << escapeString(field_content);
        if (i < label_fields.size() - 1) query << ", ";
    }
    
    // Last field (no comma)
    query << ", " << escapeString(getArrayAsString(record["carcinogenesis_and_mutagenesis_and_impairment_of_fertility"]));
    query << ")";
    
    if (!db_conn->executeQuery(query.str())) {
        std::string error_msg = db_conn->getLastError();
        if (isDuplicateKeyError(error_msg)) {
            std::string product_ndc = getFirstElement(record["openfda"], "product_ndc");
            std::cout << "ℹ️  Record already exists (product_ndc: " << product_ndc << ")" << std::endl;
            return true; // Consider duplicate as success to continue processing
        } else {
            std::cerr << "Error inserting drug label record: " << error_msg << std::endl;
            return false;
        }
    }
    
    return true;
}

bool DatabaseManager::insertFDANDCRecord(const Json::Value& record) {
    // Helper to safely get first array element or string value
    auto getFirstElement = [this](const Json::Value& parent, const std::string& key) -> std::string {
        if (!parent.isMember(key)) return "";
        
        const Json::Value& value = parent[key];
        if (value.isArray() && !value.empty()) {
            return value[0].asString();
        } else if (value.isString()) {
            return value.asString();
        }
        return "";
    };
    
    // Get and format product_ndc
    std::string product_ndc = getFirstElement(record, "product_ndc");
    std::string formatted_ndc = formatNDCToStandard(product_ndc);
    
    if (formatted_ndc.empty()) {
        std::cout << "⚠️  Skipping record with invalid product_ndc: " << product_ndc << std::endl;
        return true; // Skip but don't fail the entire process
    }
    
    std::stringstream query;
    query << "INSERT INTO fda_ndc_data (";
    query << "product_ndc, manufacturer_name, unii, product_type, spl_set_id, route, ";
    query << "generic_name, brand_name, substance_name, spl_id, package_ndc, ";
    query << "application_number, rxcui, pharm_class_moa, pharm_class_epc, ";
    query << "pharm_class_cs, nui, pharm_class_pe, dosage_form, ";
    query << "is_original_packager, original_packager_product_ndc, upc";
    query << ") VALUES (";
    
    query << escapeString(formatted_ndc) << ", ";
    
    // Use labeler_name for manufacturer_name (correct field name from FDA API)
    query << escapeString(getFirstElement(record, "labeler_name")) << ", ";
    
    // Handle openfda nested object fields
    std::string unii = "";
    if (record.isMember("openfda") && record["openfda"].isMember("unii")) {
        unii = getArrayAsString(record["openfda"]["unii"]);
    }
    query << escapeString(unii) << ", ";
    
    query << escapeString(getFirstElement(record, "product_type")) << ", ";
    query << escapeString(getFirstElement(record, "spl_id")) << ", ";  // spl_id is available in API
    
    // Handle route from openfda or direct field
    std::string route = "";
    if (record.isMember("openfda") && record["openfda"].isMember("route")) {
        route = getArrayAsString(record["openfda"]["route"]);
    } else if (record.isMember("route")) {
        route = getArrayAsString(record["route"]);
    }
    query << escapeString(route) << ", ";
    
    query << escapeString(getFirstElement(record, "generic_name")) << ", ";
    
    // Handle brand_name - could be in brand_name or brand_name_base
    std::string brand_name = getFirstElement(record, "brand_name");
    if (brand_name.empty()) {
        brand_name = getFirstElement(record, "brand_name_base");
    }
    query << escapeString(brand_name) << ", ";
    
    // Handle substance_name from active_ingredients
    std::string substance_names = "";
    if (record.isMember("active_ingredients") && record["active_ingredients"].isArray()) {
        std::stringstream ss;
        bool first = true;
        for (const auto& ingredient : record["active_ingredients"]) {
            if (!first) ss << "; ";
            if (ingredient.isMember("name")) {
                ss << ingredient["name"].asString();
                if (ingredient.isMember("strength")) {
                    ss << " (" << ingredient["strength"].asString() << ")";
                }
            }
            first = false;
        }
        substance_names = ss.str();
    } else if (record.isMember("openfda") && record["openfda"].isMember("substance_name")) {
        substance_names = getArrayAsString(record["openfda"]["substance_name"]);
    }
    query << escapeString(substance_names) << ", ";
    
    query << escapeString(getFirstElement(record, "spl_id")) << ", ";  // Use spl_id again for compatibility
    
    // Extract package NDCs from packaging array
    std::string package_ndcs = "";
    if (record.isMember("packaging") && record["packaging"].isArray()) {
        std::stringstream ss;
        bool first = true;
        for (const auto& pkg : record["packaging"]) {
            if (!first) ss << "; ";
            if (pkg.isMember("package_ndc")) {
                ss << pkg["package_ndc"].asString();
            }
            first = false;
        }
        package_ndcs = ss.str();
    }
    query << escapeString(package_ndcs) << ", ";
    
    // Handle fields that may be in openfda object
    std::string application_number = "";
    if (record.isMember("openfda") && record["openfda"].isMember("application_number")) {
        application_number = getArrayAsString(record["openfda"]["application_number"]);
    }
    query << escapeString(application_number) << ", ";
    
    std::string rxcui = "";
    if (record.isMember("openfda") && record["openfda"].isMember("rxcui")) {
        rxcui = getArrayAsString(record["openfda"]["rxcui"]);
    }
    query << escapeString(rxcui) << ", ";
    
    // Pharmaceutical class fields (usually in openfda)
    std::string pharm_class_moa = "";
    if (record.isMember("openfda") && record["openfda"].isMember("pharm_class_moa")) {
        pharm_class_moa = getArrayAsString(record["openfda"]["pharm_class_moa"]);
    }
    query << escapeString(pharm_class_moa) << ", ";
    
    std::string pharm_class_epc = "";
    if (record.isMember("openfda") && record["openfda"].isMember("pharm_class_epc")) {
        pharm_class_epc = getArrayAsString(record["openfda"]["pharm_class_epc"]);
    }
    query << escapeString(pharm_class_epc) << ", ";
    
    std::string pharm_class_cs = "";
    if (record.isMember("openfda") && record["openfda"].isMember("pharm_class_cs")) {
        pharm_class_cs = getArrayAsString(record["openfda"]["pharm_class_cs"]);
    }
    query << escapeString(pharm_class_cs) << ", ";
    
    std::string nui = "";
    if (record.isMember("openfda") && record["openfda"].isMember("nui")) {
        nui = getArrayAsString(record["openfda"]["nui"]);
    }
    query << escapeString(nui) << ", ";
    
    std::string pharm_class_pe = "";
    if (record.isMember("openfda") && record["openfda"].isMember("pharm_class_pe")) {
        pharm_class_pe = getArrayAsString(record["openfda"]["pharm_class_pe"]);
    }
    query << escapeString(pharm_class_pe) << ", ";
    
    query << escapeString(getFirstElement(record, "dosage_form")) << ", ";
    
    // Handle is_original_packager - this field might not exist, default to false
    bool is_orig_packager = false;
    if (record.isMember("is_original_packager")) {
        std::string orig_val = getFirstElement(record, "is_original_packager");
        is_orig_packager = (orig_val == "true" || orig_val == "1" || record["is_original_packager"].asBool());
    }
    query << (is_orig_packager ? "TRUE" : "FALSE") << ", ";
    
    // Original packager product NDC - likely not available in API data
    query << "NULL" << ", ";
    
    // UPC codes - likely not available in basic API data
    std::string upc = "";
    if (record.isMember("openfda") && record["openfda"].isMember("upc")) {
        upc = getArrayAsString(record["openfda"]["upc"]);
    }
    query << escapeString(upc);
    
    query << ")";
    
    if (!db_conn->executeQuery(query.str())) {
        std::string error_msg = db_conn->getLastError();
        if (isDuplicateKeyError(error_msg)) {
            std::cout << "ℹ️  Record already exists (product_ndc: " << formatted_ndc << ")" << std::endl;
            return true; // Consider duplicate as success to continue processing
        } else {
            std::cerr << "Error inserting FDA NDC record: " << error_msg << std::endl;
            return false;
        }
    }
    
    return true;
}