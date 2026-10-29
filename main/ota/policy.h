#pragma once
#include "sdkconfig.h"

namespace firmware_ota {
// 本项目按用户要求关闭远程固件刷写。云端激活、服务配置和时间同步
// 仍需访问版本查询接口，因此不要把 OTA_URL 或 CheckVersion 一并删除。
// 同一策略同时约束开机升级、MCP 升级和底层写入，避免只关掉一个入口。
#if CONFIG_ENABLE_REMOTE_FIRMWARE_OTA
inline constexpr bool kEnabled = true;
#else
inline constexpr bool kEnabled = false;
#endif
}  // namespace firmware_ota
