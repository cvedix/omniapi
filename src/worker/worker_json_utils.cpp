#include "worker/worker_json_utils.h"

namespace worker {

void mergeJsonInto(Json::Value &target, const Json::Value &source) {
  if (!source.isObject()) return;
  for (const auto &key : source.getMemberNames()) {
    const Json::Value &srcVal = source[key];
    if (srcVal.isObject() && target.isMember(key) && target[key].isObject()) {
      mergeJsonInto(target[key], srcVal);
    } else {
      target[key] = srcVal;
    }
  }
}

const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64Idx(const std::string &safe, size_t j) {
  char c = safe[j];
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

std::vector<uint8_t> base64Decode(const std::string &encoded) {
  std::vector<uint8_t> out;
  if (encoded.empty()) return out;
  std::string safe;
  for (char c : encoded) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')
      safe += c;
  }
  size_t len = safe.size();
  if (len == 0 || (len % 4) != 0) return out;
  out.reserve((len / 4) * 3);
  for (size_t i = 0; i < len; i += 4) {
    if (safe[i] == '=' || safe[i + 1] == '=') break;
    int n0 = base64Idx(safe, i), n1 = base64Idx(safe, i + 1),
        n2 = (safe[i + 2] == '=') ? -1 : base64Idx(safe, i + 2),
        n3 = (safe[i + 3] == '=') ? -1 : base64Idx(safe, i + 3);
    if (n0 < 0 || n1 < 0) break;
    out.push_back(static_cast<uint8_t>((n0 << 2) | (n1 >> 4)));
    if (n2 >= 0) {
      out.push_back(static_cast<uint8_t>(((n1 & 15) << 4) | (n2 >> 2)));
      if (n3 >= 0)
        out.push_back(static_cast<uint8_t>(((n2 & 3) << 6) | n3));
    }
  }
  return out;
}

} // namespace worker
