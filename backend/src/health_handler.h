#pragma once

#include <httplib.h>
#include <string>

namespace teleop::health {

/**
 * 注册 /health 与 /ready 端点到 svr。
 * 注意：应在 svr.listen() 前调用。
 */
void register_handlers(httplib::Server& svr,
                   const std::string& database_url,
                   const std::string& zlm_api_url,
                   const std::string& version);

} // namespace teleop::health
