#include "UserController.h"
#include "../json.hpp"
#include "SqlConnPool.h"
#include "../util/CryptoUtil.h"

using json = nlohmann::json;

namespace UserController {

bool registerUser(const std::string& username, const std::string& password) {
    MYSQL* sql = nullptr;
    SqlConnRAII conn(&sql, SqlConnPool::Instance());
    if(!sql) return false;

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT id FROM user WHERE username='%s' LIMIT 1",
             username.c_str());
    if(mysql_query(sql, query) != 0) {
        return false;
    }
    MYSQL_RES* res = mysql_store_result(sql);
    bool exists = (res && mysql_fetch_row(res));
    if(res) mysql_free_result(res);
    if(exists) return false;   

    char insert[256];
    snprintf(insert, sizeof(insert),
             "INSERT INTO user(username, passwd) VALUES('%s', '%s')",
             username.c_str(), password.c_str());
    if(mysql_query(sql, insert) != 0) {
        return false;
    }
    return true;
}

bool checkLogin(const std::string& username, const std::string& password) {
    MYSQL* sql = nullptr;
    SqlConnRAII conn(&sql, SqlConnPool::Instance());
    if(!sql) return false;

    char order[256] = { 0 };
    snprintf(order, 256, "SELECT username, passwd FROM user WHERE username='%s' LIMIT 1", username.c_str());

    if(mysql_query(sql, order)) {
        return false;
    }

    MYSQL_RES *res = mysql_store_result(sql);
    if(!res) return false;

    bool flag = false;
    while(MYSQL_ROW row = mysql_fetch_row(res)) {
        std::string dbPassword(row[1]);
        if(password == dbPassword) {
            flag = true;
        }
    }
    mysql_free_result(res);
    return flag;
}

void handleRegister(std::shared_ptr<HttpData> httpData) {
    json root;
    try {
        root = json::parse(httpData->getInBuffer().peekAllAsString());
    } catch (...) {
        httpData->sendResponse(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid JSON\"}");
        return;
    }

    std::string username = root["username"];
    std::string password = root["password"];

    if (!registerUser(username, password)) {
        httpData->sendResponse(200, "application/json", "{\"ok\":false,\"msg\":\"User exists\"}");
        return;
    }

    json resp;
    resp["ok"] = true;
    httpData->sendResponse(200, "application/json", resp.dump());
}

void handleLogin(std::shared_ptr<HttpData> httpData) {
    json root;
    try {
        root = json::parse(httpData->getInBuffer().peekAllAsString());
    } catch (...) {
        httpData->sendResponse(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid JSON\"}");
        return;
    }

    if (!root.contains("username") || !root.contains("password")) {
        httpData->sendResponse(400, "application/json", "{\"ok\":false,\"msg\":\"Missing fields\"}");
        return;
    }

    std::string username = root["username"];
    std::string password = root["password"];

    if (!checkLogin(username, password)) {
        httpData->sendResponse(200, "application/json", "{\"ok\":false,\"msg\":\"Wrong username or password\"}");
        return;
    }

    std::string token = CryptoUtil::generateJWT(username);

    json resp;
    resp["ok"] = true;
    resp["token"] = token;

    httpData->sendResponse(200, "application/json", resp.dump());
}

} // namespace UserController
