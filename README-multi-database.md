# Multi-Database Configuration Guide

This application now supports both MySQL and PostgreSQL databases with the ability to switch between them using a single configuration file.

## Configuration Format

### Multi-Database Configuration (`database-multi.nfo`)

```ini
# OpenFDA Multi-Database Configuration
# Set 'active_database' to choose which database to use

# Active database selection: mysql or postgresql  
active_database: postgresql

# MySQL Configuration
[mysql]
host: localhost
port: 3306
user: your_username
password: your_password
database: openfda
schema: openfda

# PostgreSQL Configuration  
[postgresql]
host: miniserver.local
port: 5432
user: jason
password: your_password
database: openfda
schema: public
```

### Single-Database Configuration (Legacy)

```ini
# Traditional single database configuration (still supported)
type: postgresql
host: miniserver.local
port: 5432
user: jason
password: your_password
database: openfda
schema: public
```

## Usage

### Using Multi-Database Configuration

```bash
# Use PostgreSQL (as specified by active_database)
./openfda_ndc_downloader -c database-multi.nfo --clear-ndc

# Switch databases by editing the config file:
# Change: active_database: postgresql 
# To:     active_database: mysql
```

### Automatic Database Detection

The application automatically detects the configuration format:
- **Files with `[sections]`** → Multi-database format
- **Files without sections** → Single-database format

### Database Creation

The application automatically:
- ✅ **MySQL**: Creates database if missing
- ✅ **PostgreSQL**: Creates database if missing  
- ✅ **PostgreSQL**: Creates schema if needed
- ✅ **Both**: Creates tables as required

## Database-Specific Features

### MySQL
- Uses `JSON` data type
- Uses `AUTO_INCREMENT` for primary keys
- Creates database with `utf8mb4` encoding

### PostgreSQL  
- Uses `JSONB` data type (binary JSON, faster)
- Uses `SERIAL` for primary keys
- Creates database with UTF-8 encoding
- Supports custom schemas beyond `public`

## File Search Priority

Configuration files are searched in:
1. **Specified path** (if using `-c /path/to/file`)
2. **Current directory** (`./database.nfo`)
3. **Parent directory** (`../database.nfo`)

## Examples

```bash
# Interactive mode with multi-database config
./openfda_ndc_downloader -i -c database-multi.nfo

# Bulk download with PostgreSQL  
./openfda_ndc_downloader --ndc-bulk -c database-multi.nfo

# Switch to MySQL by changing active_database in config, then:
./openfda_ndc_downloader --drugsfda -c database-multi.nfo
```

## Benefits

- **Single config file** for both database types
- **Easy switching** by changing one line
- **Environment flexibility** (dev/staging/production)
- **Backward compatibility** with existing configs
- **Automatic database creation** and setup