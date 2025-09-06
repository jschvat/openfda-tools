# OpenFDA Data Downloader

This C++ program downloads drug data from OpenFDA sources, parses the JSON responses, and stores the data in a MySQL database. It supports both the NDC API and bulk DrugsFDA downloads.

## Features

- **Dual Data Sources**: Support for NDC API and DrugsFDA bulk downloads
- **Bulk Processing**: Downloads and extracts ZIP files for efficient bulk data processing
- **Flexible Database Schema**: Creates appropriate tables for different data types
- **Batch Processing**: Downloads NDC data from API in batches with pagination
- **JSON Parsing**: Extracts and stores complex nested data structures
- **Error Handling**: Comprehensive error checking and logging
- **Command Line Options**: Easy switching between data sources

## Prerequisites

### System Requirements

- Linux/Unix system with build tools
- MySQL server running
- CMake 3.10 or higher
- C++17 compatible compiler (g++, clang++)
- `unzip` utility (for bulk download extraction)

### Required Libraries

Install the following development libraries:

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential cmake unzip
sudo apt-get install libcurl4-openssl-dev
sudo apt-get install libmysqlclient-dev
sudo apt-get install libjsoncpp-dev

# CentOS/RHEL/Fedora
sudo yum install gcc-c++ cmake unzip
sudo yum install libcurl-devel
sudo yum install mysql-devel
sudo yum install jsoncpp-devel

# Or for newer versions:
sudo dnf install gcc-c++ cmake unzip
sudo dnf install libcurl-devel
sudo dnf install mysql-devel
sudo dnf install jsoncpp-devel
```

## Database Setup

1. Ensure you have MySQL server running and user credentials with database creation privileges.

2. Create a `database.nfo` configuration file:

```
host: your_mysql_host
port: 3306
user: your_username
password: your_password
database: openfda
schema: ndc_files
```

**Note**: The program will automatically create the database and tables if they don't exist.

## Building the Program

1. Clone or download the source files
2. Build using CMake:

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

The program automatically connects using your `database.nfo` configuration file and supports two data sources:

### Command Line Syntax

```bash
./openfda_ndc_downloader [OPTIONS] [config_file]
```

### Options

- `-n, --ndc`: Use NDC API (default)
- `-d, --drugsfda`: Use DrugsFDA bulk download
- `-c, --config FILE`: Specify config file (default: database.nfo)
- `-h, --help`: Show help message

### Examples

**NDC API Processing (default):**

```bash
./openfda_ndc_downloader                    # Uses database.nfo
./openfda_ndc_downloader --ndc              # Explicitly use NDC API
./openfda_ndc_downloader -c myconfig.nfo    # Use custom config file
```

**DrugsFDA Bulk Download:**

```bash
./openfda_ndc_downloader --drugsfda         # Download bulk ZIP file
```

### What happens when you run the program:

1. ✅ **Loads** database configuration from file
2. ✅ **Connects** to MySQL server
3. ✅ **Creates** database if it doesn't exist
4. ✅ **Creates** appropriate table schema
5. ✅ **Downloads** data (API batches or bulk ZIP file)
6. ✅ **Processes** and inserts data with progress reporting
7. ✅ **Completes** with success summary

## Database Schema

The program creates different tables based on the data source:

### NDC Data Table (`ndc_data`)

Created when using NDC API (`--ndc` option):

- `id`: Auto-increment primary key
- `product_ndc`: NDC number
- `generic_name`: Generic drug name
- `brand_name`: Brand name
- `labeler_name`: Manufacturer/labeler name
- `product_type`: Type of product (e.g., "HUMAN OTC DRUG")
- `dosage_form`: Form of medication
- `route`: Administration route
- `marketing_start_date`: When marketing began
- `marketing_end_date`: When marketing ended
- `product_id`: Product identifier
- `application_number`: FDA application number
- `brand_name_base`: Base brand name
- `brand_name_suffix`: Brand name suffix
- `active_ingredients`: JSON field for active ingredients
- `packaging`: JSON field for packaging information
- `openfda_data`: JSON field for additional OpenFDA data
- `created_at`: Timestamp when record was inserted

### DrugsFDA Data Table (`drugsfda_data`)

Created when using DrugsFDA bulk download (`--drugsfda` option):

- `id`: Auto-increment primary key
- `application_number`: FDA application number
- `sponsor_name`: Drug sponsor/manufacturer name
- `submissions`: JSON field containing submission history
- `products`: JSON field containing product information
- `openfda_data`: JSON field for additional OpenFDA harmonized data
- `created_at`: Timestamp when record was inserted

## Program Flow

### NDC API Mode (default)

1. Connects to MySQL database
2. Creates the `ndc_data` table if it doesn't exist
3. Downloads data from OpenFDA NDC API in batches of 1000 records
4. Parses JSON responses and extracts relevant fields
5. Inserts data into MySQL table
6. Continues until all available data is processed

### DrugsFDA Bulk Mode (`--drugsfda`)

1. Connects to MySQL database
2. Creates the `drugsfda_data` table if it doesn't exist
3. Downloads the bulk ZIP file (drug-drugsfda-0001-of-0001.json.zip)
4. Extracts the ZIP file to a temporary directory
5. Processes the large JSON file in memory
6. Inserts all records into the MySQL table
7. Cleans up temporary files

## Error Handling

The program includes error handling for:

- Database connection failures
- HTTP request failures
- JSON parsing errors
- SQL insertion errors
- Missing or malformed data

## Performance Notes

### NDC API Mode

- Respects OpenFDA API rate limits with small delays between requests
- Processes data in manageable 1000-record batches
- Suitable for incremental updates or smaller datasets

### DrugsFDA Bulk Mode

- Downloads and processes entire dataset (~100MB+ ZIP file)
- More efficient for complete data refresh
- Requires sufficient disk space and memory
- Single large transaction for optimal database performance

## Technical Notes

- Date fields are converted from YYYYMMDD format to MySQL DATE format
- Complex nested data (ingredients, packaging, submissions) is stored as JSON
- The program creates indexes on commonly queried fields for better performance
- Temporary files are automatically cleaned up after processing
- Both UTF-8 and binary data are handled correctly

## Disclaimer

This data comes from the OpenFDA API and should not be used for medical decisions. Always consult healthcare professionals for drug information.

Data Source Options and Their Mappings:

1. -n, --ndc (NDC API) - DEFAULT

- URL: https://api.fda.gov/drug/ndc.json (API calls with pagination)
- Table: ndc_data
- Method: Multiple API requests (slower but always current)

2. -b, --ndc-bulk (NDC Bulk Download)

- URL: https://download.open.fda.gov/drug/ndc/drug-ndc-0001-of-0001.json.zip
- Table: ndc_data
- Method: Single large ZIP file download (faster)

3. -d, --drugsfda (DrugsFDA Bulk)

- URL: https://download.open.fda.gov/drug/drugsfda/drug-drugsfda-0001-of-0001.json.zip
- Table: drugsfda_data
- Method: Single large ZIP file (different dataset)

4. -l, --drug-label (Drug Label Bulk)

- URL: https://download.open.fda.gov/drug/label/drug-label-XXXX-of-0013.json.zip (13 files)
- Table: drug_label_data
- Method: 13 separate ZIP files (comprehensive label data)

5. -f, --fda-ndc (FDA NDC Bulk - Normalized)

- URL: https://download.open.fda.gov/drug/ndc/drug-ndc-0001-of-0001.json.zip
- Table: fda_ndc_data
- Method: Same file as NDC bulk but different table/processing

Key Differences:

- NDC API vs NDC Bulk: Same data, different delivery method
- NDC Bulk vs FDA NDC Bulk: Same URL but different table (ndc_data vs fda_ndc_data) and likely different
  processing logic
- Drug Label: Comprehensive labeling data across 13 files
- DrugsFDA: Completely different dataset (FDA approval data)

The confusion likely comes from NDC_BULK and FDA_NDC_BULK using the same URL but creating different tables.
