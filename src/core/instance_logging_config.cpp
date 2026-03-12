#include "core/instance_logging_config.h"
#include <mutex>
#include <unordered_map>

namespace InstanceLoggingConfig {

static std::unordered_map<std::string, bool> s_enabled;
static std::mutex s_mutex;

void setEnabled(const std::string &instance_id, bool enabled) {
  std::lock_guard<std::mutex> lock(s_mutex);
  s_enabled[instance_id] = enabled;
}

bool isEnabled(const std::string &instance_id) {
  std::lock_guard<std::mutex> lock(s_mutex);
  auto it = s_enabled.find(instance_id);
  return it != s_enabled.end() && it->second;
}

void remove(const std::string &instance_id) {
  std::lock_guard<std::mutex> lock(s_mutex);
  s_enabled.erase(instance_id);
}

void loadFromConfig(const std::string &instance_id,
                    const Json::Value &logging_section) {
  if (!logging_section.isObject() || !logging_section.isMember("enabled"))
    return;
  if (logging_section["enabled"].isBool())
    setEnabled(instance_id, logging_section["enabled"].asBool());
}

} // namespace InstanceLoggingConfig
