#pragma once
#include "../fb2k.h"
#include <string>

namespace pui {

bool http_get(const std::string& url, std::string& body, abort_callback& ab, const char* tag,
              size_t max_bytes = 2u << 20);

}
