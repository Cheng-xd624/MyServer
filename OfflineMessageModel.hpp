#pragma once

#include <vector>
#include <string>

/**
 * @brief 离线消息模型类
 * 
 * 该类用于处理离线消息的存储和管理。
 */
class OfflineMessageModel {
public:
    static OfflineMessageModel* instance();  
    
    OfflineMessageModel(const OfflineMessageModel&) = delete; // 禁止拷贝构造
    OfflineMessageModel& operator=(const OfflineMessageModel&) = delete; // 禁止拷贝赋值

    // 插入一条离线消息到数据库
    bool insert(int userId, const std::string& message);

    // 查询某个用户的所有离线消息
    std::vector<std::string> query(int userId);

    // 删除某个用户的所有离线消息
    bool remove(int userId);
    
private:
    OfflineMessageModel() = default; // 私有构造函数，确保单例模式
};