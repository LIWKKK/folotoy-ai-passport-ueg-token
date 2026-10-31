// main/we_http.h —— 极薄的 HTTPS GET 封装。
// 只做一件事:带 Bearer 头 GET 一个 JSON 地址,把响应体拷进调用方缓冲。
// 证书校验走 ESP-IDF 内置证书包(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE),
// 不额外存根证书,省下本板紧张的 Flash/RAM。
#pragma once

#include <stddef.h>

// 成功返回 0,失败返回负值。out 总是以 '\0' 结尾。
int we_http_get(const char *url, const char *bearer_token, char *out,
                size_t out_len);
