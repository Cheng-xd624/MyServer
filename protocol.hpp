#pragma once

#include<string>
#include <nlohmann/json.hpp>

// 给nlohmann::json起个新名字
using json = nlohmann::json;

// 消息类型枚举(msgid)
// 客户端和服务器约定：每台json消息必须带"msgid"字段
enum EnMsgType
{
    LOGIN_MSG       = 1,    //登录请求
    LOGIN_MSG_ACK   = 2,    //登录响应
    REG_MSG         = 3,    //注册请求
    REG_MSG_ACK     = 4,    //注册响应
    ONE_CHAT_MSG    = 5,    //一对一聊天
    ADD_FRIEND_MSG  = 6,    //添加好友请求，不用增加添加好友响应，客户端直接显示添加好友成功或失败
    ACCEPT_FRIEND_MSG = 7,  //收到好友请求
    CREATE_GROUP_MSG= 8,    //创建群组
    ADD_GROUP_MSG   = 9,    //加入群组
    GROUP_CHAT_MSG  = 10,   //群聊
    LOGINOUT_MSG    = 11,   //注销登录
    FRIEND_REQUEST_NOTICE = 12, //推送：好友申请/同意通知，用type区分
};

// 错误码
enum ErrCode
{
    SUCCESS            = 0,   // 成功
    LOGIN_FAILED       = 1,   // 登录失败
    REG_FAILED         = 2,   // 注册失败
    USER_NOT_EXIST     = 3,   // 用户不存在
    USER_ALREADY_EXIST = 4,   // 用户已存在
    WRONG_PASSWORD     = 5,   // 密码错误
    USER_OFFLINE       = 6,   // 对方不在线
};


// ============================================================
// 协议格式说明（写在注释里，方便查阅）
// ============================================================
//
// 登录请求：
// {
//     "msgid": 1,
//     "id": 1001,
//     "password": "123456"
// }
//
// 登录响应：
// {
//     "msgid": 2,
//     "errno": 0,
//     "id": 1001,
//     "name": "zhangsan",
//     "friends": [ ... ]
// }
//
// 注册请求：
// {
//     "msgid": 3,
//     "name": "zhangsan",
//     "password": "123456"
// }
//
// 单聊消息：
// {
//     "msgid": 5,
//     "from": 1001,
//     "to": 1002,
//     "message": "hello"
// }
//
// 群聊消息：
// {
//     "msgid": 9,
//     "from": 1001,
//     "groupid": 1,
//     "message": "hello everyone"
// }
// ============================================================