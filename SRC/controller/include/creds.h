#pragma once
#include <Arduino.h>

/* ============================================================
 * 账号与权限（NVS 持久化, Preferences "creds"）
 *   - 管理员: 用户名固定 "admin", 密码可改(默认 WEB_PASSWORD)
 *   - 操作员: 用户名/密码由管理员自定义(默认未启用), 仅只读浏览
 *   - 校验入口: /api/login(Web) / #PWD(串口屏) / 危险命令密码
 * 完整说明见 docs/账号与权限.md
 * ============================================================ */

void   credsInit();                                  /* 初始化(写入默认管理密码) */
String credsAdminUser();                             /* 恒为 "admin" */
String credsStoredAdminPw();                         /* 读取 NVS 中存储的管理密码(读不到返回空) */
bool   credsIsDefaultPw();                           /* 存储密码是否为默认 WEB_PASSWORD */
bool   credsCheckLogin(const String& user, const String& pw, String& role);
                                 /* 匹配: admin / operator; 均不匹配返回 false */
bool   credsAdminPwOk(const String& pw);             /* 校验管理密码(解锁/危险命令) */
bool   credsSetAdminPw(const String& oldPw, const String& fresh);
                                 /* 修改管理密码(校验旧密码); 失败返回 false */
bool   credsSetOperator(const String& user, const String& pw);
                                 /* 设置/更新操作员(user 空=清除); 返回是否成功 */
String credsOpUser();                                /* 操作员用户名("" = 未启用) */
