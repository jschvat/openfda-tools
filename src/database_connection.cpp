#include "database_connection.h"
#include <iostream>
#include <sstream>
#include <cstring>

// MySQL Connection Implementation
MySQLConnection::MySQLConnection() : connection(nullptr) {
    mysql_library_init(0, NULL, NULL);
}

MySQLConnection::~MySQLConnection() {
    disconnect();
    mysql_library_end();
}

bool MySQLConnection::connect(const DatabaseConfig& config) {
    disconnect();
    
    connection = mysql_init(NULL);
    if (!connection) {
        last_error = "Failed to initialize MySQL connection";
        return false;
    }
    
    // Set connection timeout
    unsigned int timeout = 10;
    mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    
    // Connect to MySQL
    if (!mysql_real_connect(connection,
                           config.host.c_str(),
                           config.user.c_str(),
                           config.password.c_str(),
                           config.database.c_str(),
                           config.port,
                           NULL, 0)) {
        last_error = mysql_error(connection);
        mysql_close(connection);
        connection = nullptr;
        return false;
    }
    
    // Set charset to UTF-8
    if (mysql_set_character_set(connection, "utf8")) {
        last_error = "Failed to set UTF-8 character set";
        return false;
    }
    
    return true;
}

void MySQLConnection::disconnect() {
    if (connection) {
        mysql_close(connection);
        connection = nullptr;
    }
}

bool MySQLConnection::isConnected() const {
    return connection != nullptr && mysql_ping(connection) == 0;
}

bool MySQLConnection::executeQuery(const std::string& query) {
    if (!connection) {
        last_error = "No database connection";
        return false;
    }
    
    if (mysql_query(connection, query.c_str())) {
        last_error = mysql_error(connection);
        return false;
    }
    
    // Free result if any
    MYSQL_RES* result = mysql_store_result(connection);
    if (result) {
        mysql_free_result(result);
    }
    
    return true;
}

bool MySQLConnection::executeQuery(const std::string& query, std::vector<std::vector<std::string>>& results) {
    if (!connection) {
        last_error = "No database connection";
        return false;
    }
    
    if (mysql_query(connection, query.c_str())) {
        last_error = mysql_error(connection);
        return false;
    }
    
    MYSQL_RES* result = mysql_store_result(connection);
    if (!result) {
        last_error = mysql_error(connection);
        return false;
    }
    
    results.clear();
    MYSQL_ROW row;
    int num_fields = mysql_num_fields(result);
    
    while ((row = mysql_fetch_row(result))) {
        std::vector<std::string> row_data;
        for (int i = 0; i < num_fields; i++) {
            row_data.push_back(row[i] ? row[i] : "");
        }
        results.push_back(row_data);
    }
    
    mysql_free_result(result);
    return true;
}

std::string MySQLConnection::escapeString(const std::string& str) {
    if (!connection) return str;
    
    char* escaped = new char[str.length() * 2 + 1];
    mysql_real_escape_string(connection, escaped, str.c_str(), str.length());
    std::string result(escaped);
    delete[] escaped;
    return result;
}

std::string MySQLConnection::getLastError() const {
    return last_error;
}

// PostgreSQL Connection Implementation
PostgreSQLConnection::PostgreSQLConnection() : connection(nullptr) {}

PostgreSQLConnection::~PostgreSQLConnection() {
    disconnect();
}

bool PostgreSQLConnection::connect(const DatabaseConfig& config) {
    disconnect();
    
    std::ostringstream conninfo;
    conninfo << "host=" << config.host
             << " port=" << config.port
             << " dbname=" << config.database
             << " user=" << config.user
             << " password=" << config.password
             << " connect_timeout=10";
    
    connection = PQconnectdb(conninfo.str().c_str());
    
    if (PQstatus(connection) != CONNECTION_OK) {
        last_error = PQerrorMessage(connection);
        PQfinish(connection);
        connection = nullptr;
        return false;
    }
    
    // Set client encoding to UTF-8
    if (PQsetClientEncoding(connection, "UTF8") != 0) {
        last_error = "Failed to set UTF-8 encoding";
        return false;
    }
    
    return true;
}

void PostgreSQLConnection::disconnect() {
    if (connection) {
        PQfinish(connection);
        connection = nullptr;
    }
}

bool PostgreSQLConnection::isConnected() const {
    return connection != nullptr && PQstatus(connection) == CONNECTION_OK;
}

bool PostgreSQLConnection::executeQuery(const std::string& query) {
    if (!connection) {
        last_error = "No database connection";
        return false;
    }
    
    PGresult* result = PQexec(connection, query.c_str());
    
    ExecStatusType status = PQresultStatus(result);
    bool success = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
    
    if (!success) {
        last_error = PQerrorMessage(connection);
    }
    
    PQclear(result);
    return success;
}

bool PostgreSQLConnection::executeQuery(const std::string& query, std::vector<std::vector<std::string>>& results) {
    if (!connection) {
        last_error = "No database connection";
        return false;
    }
    
    PGresult* result = PQexec(connection, query.c_str());
    
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        last_error = PQerrorMessage(connection);
        PQclear(result);
        return false;
    }
    
    results.clear();
    int rows = PQntuples(result);
    int cols = PQnfields(result);
    
    for (int i = 0; i < rows; i++) {
        std::vector<std::string> row_data;
        for (int j = 0; j < cols; j++) {
            char* value = PQgetvalue(result, i, j);
            row_data.push_back(value ? value : "");
        }
        results.push_back(row_data);
    }
    
    PQclear(result);
    return true;
}

std::string PostgreSQLConnection::escapeString(const std::string& str) {
    if (!connection) return str;
    
    size_t length = str.length();
    char* escaped = new char[length * 2 + 1];
    int error;
    
    size_t escaped_length = PQescapeStringConn(connection, escaped, str.c_str(), length, &error);
    
    if (error) {
        delete[] escaped;
        return str;
    }
    
    std::string result(escaped, escaped_length);
    delete[] escaped;
    return result;
}

std::string PostgreSQLConnection::getLastError() const {
    return last_error;
}

// Factory function
std::unique_ptr<DatabaseConnection> createDatabaseConnection(DatabaseType type) {
    switch (type) {
        case DatabaseType::MYSQL:
            return std::make_unique<MySQLConnection>();
        case DatabaseType::POSTGRESQL:
            return std::make_unique<PostgreSQLConnection>();
        default:
            return nullptr;
    }
}