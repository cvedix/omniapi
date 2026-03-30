#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace worker {

/** Deep-merge source into target (objects merged recursively, other types replace). */
void mergeJsonInto(Json::Value &target, const Json::Value &source);

/** Base64 decode; returns empty vector on error. */
std::vector<uint8_t> base64Decode(const std::string &encoded);

/** Base64 encoding table (for encoding). */
extern const char kBase64Chars[];

} // namespace worker
