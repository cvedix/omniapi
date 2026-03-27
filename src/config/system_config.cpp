#include "config/system_config.h"
#include "core/env_config.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

SystemConfig &SystemConfig::getInstance() {
  static SystemConfig instance;
  return instance;
}

bool SystemConfig::loadConfig(const std::string &configPath) {
  config_path_ = configPath;

  try {
    if (!std::filesystem::exists(configPath)) {
      std::cerr << "[SystemConfig] Config file not found: " << configPath
                << std::endl;
      std::cerr << "[SystemConfig] Initializing with default configuration"
                << std::endl;

      {
        std::lock_guard<std::mutex> lock(mutex_);
        initializeDefaults();
        loaded_ = true;
      }

      // Save default config to file (outside lock to avoid deadlock)
      saveConfig(configPath);
      return true;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::ifstream file(configPath);
    if (!file.is_open()) {
      std::cerr << "[SystemConfig] Error: Failed to open config file: "
                << configPath << std::endl;
      initializeDefaults();
      loaded_ = false;
      return false;
    }

    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &config_json_, &errors)) {
      std::cerr << "[SystemConfig] Failed to parse config file: " << errors
                << std::endl;
      initializeDefaults();
      loaded_ = false;
      return false;
    }

    if (!validateConfig(config_json_)) {
      std::cerr << "[SystemConfig] Invalid config structure, using defaults"
                << std::endl;
      initializeDefaults();
      loaded_ = false;
      return false;
    }

    loaded_ = true;
    std::cerr << "[SystemConfig] Successfully loaded config from: "
              << configPath << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[SystemConfig] Exception loading config: " << e.what()
              << std::endl;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      initializeDefaults();
      loaded_ = false;
    }
    return false;
  }
}

bool SystemConfig::saveConfig(const std::string &configPath) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::string path = configPath.empty() ? config_path_ : configPath;
  if (path.empty()) {
    std::cerr << "[SystemConfig] Error: No config path specified" << std::endl;
    return false;
  }

  try {
    // Create parent directory if needed (with fallback if needed)
    std::filesystem::path filePath(path);
    if (filePath.has_parent_path()) {
      std::string parentDir = filePath.parent_path().string();
      std::string subdir = filePath.parent_path().filename().string();
      if (subdir.empty()) {
        subdir = "config"; // Default fallback subdir
      }
      parentDir = EnvConfig::resolveDirectory(parentDir, subdir);
      // Update path if fallback was used
      if (parentDir != filePath.parent_path().string()) {
        filePath = parentDir / filePath.filename();
        path = filePath.string();
      }
    }

    std::ofstream file(path);
    if (!file.is_open()) {
      std::cerr << "[SystemConfig] Error: Failed to open file for writing: "
                << path << std::endl;
      return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  "; // 2 spaces for indentation
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(config_json_, &file);
    file.close();

    if (configPath.empty()) {
      config_path_ = path;
    }

    std::cerr << "[SystemConfig] Successfully saved config to: " << path
              << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[SystemConfig] Exception saving config: " << e.what()
              << std::endl;
    return false;
  }
}

// Default auto_device_list: GPU/accelerator devices before CPU (prioritize GPU)
static std::vector<std::string> getDefaultAutoDeviceList() {
  return {
      "hailo.auto",   "blaize.auto", "tensorrt.1", "rknn.auto", "tensorrt.2",
      "cavalry",      "openvino.VPU", "openvino.GPU", "openvino.CPU",
      "snpe.dsp",     "snpe.aip",    "mnn.auto",   "armnn.GpuAcc", "armnn.CpuAcc",
      "armnn.CpuRef", "memx.memx",   "memx.cpu",
  };
}

void SystemConfig::initializeDefaults() {
  config_json_ = Json::Value(Json::objectValue);

  // auto_device_list (GPU-first default)
  Json::Value autoDeviceList(Json::arrayValue);
  for (const auto &d : getDefaultAutoDeviceList()) {
    autoDeviceList.append(d);
  }
  config_json_["auto_device_list"] = autoDeviceList;

  // decoder_priority_list
  Json::Value decoderPriorityList(Json::arrayValue);
  decoderPriorityList.append("blaize.auto");
  decoderPriorityList.append("rockchip");
  decoderPriorityList.append("nvidia.1");
  decoderPriorityList.append("intel.1");
  decoderPriorityList.append("software");
  config_json_["decoder_priority_list"] = decoderPriorityList;

  // gstreamer
  Json::Value gstreamer(Json::objectValue);

  // decode_pipelines
  Json::Value decodePipelines(Json::objectValue);

  Json::Value autoPipeline(Json::objectValue);
  autoPipeline["pipeline"] = "decodebin ! videoconvert";
  Json::Value autoCaps(Json::arrayValue);
  autoCaps.append("H264");
  autoCaps.append("HEVC");
  autoCaps.append("VP9");
  autoCaps.append("VC1");
  autoCaps.append("AV1");
  autoCaps.append("MJPEG");
  autoPipeline["capabilities"] = autoCaps;
  decodePipelines["auto"] = autoPipeline;

  Json::Value jetsonPipeline(Json::objectValue);
  jetsonPipeline["pipeline"] = "parsebin ! nvv4l2decoder ! nvvidconv";
  Json::Value jetsonCaps(Json::arrayValue);
  jetsonCaps.append("H264");
  jetsonCaps.append("HEVC");
  jetsonPipeline["capabilities"] = jetsonCaps;
  decodePipelines["jetson"] = jetsonPipeline;

  Json::Value nvidiaPipeline(Json::objectValue);
  nvidiaPipeline["pipeline"] = "decodebin ! nvvideoconvert ! videoconvert";
  Json::Value nvidiaCaps(Json::arrayValue);
  nvidiaCaps.append("H264");
  nvidiaCaps.append("HEVC");
  nvidiaCaps.append("VP9");
  nvidiaCaps.append("AV1");
  nvidiaCaps.append("MJPEG");
  nvidiaPipeline["capabilities"] = nvidiaCaps;
  decodePipelines["nvidia"] = nvidiaPipeline;

  Json::Value msdkPipeline(Json::objectValue);
  msdkPipeline["pipeline"] = "decodebin ! msdkvpp ! videoconvert";
  Json::Value msdkCaps(Json::arrayValue);
  msdkCaps.append("H264");
  msdkCaps.append("HEVC");
  msdkCaps.append("VP9");
  msdkCaps.append("VC1");
  msdkPipeline["capabilities"] = msdkCaps;
  decodePipelines["msdk"] = msdkPipeline;

  Json::Value vaapiPipeline(Json::objectValue);
  vaapiPipeline["pipeline"] = "decodebin ! vaapipostproc ! videoconvert";
  Json::Value vaapiCaps(Json::arrayValue);
  vaapiCaps.append("H264");
  vaapiCaps.append("HEVC");
  vaapiCaps.append("VP9");
  vaapiCaps.append("AV1");
  vaapiPipeline["capabilities"] = vaapiCaps;
  decodePipelines["vaapi"] = vaapiPipeline;

  gstreamer["decode_pipelines"] = decodePipelines;

  // plugin_rank
  Json::Value pluginRank(Json::objectValue);
  pluginRank["nvv4l2decoder"] = "257";
  pluginRank["nvjpegdec"] = "257";
  pluginRank["nvjpegenc"] = "257";
  pluginRank["nvvidconv"] = "257";
  pluginRank["msdkvpp"] = "257";
  pluginRank["vaapipostproc"] = "257";
  pluginRank["vpldec"] = "257";
  pluginRank["qsv"] = "300";
  pluginRank["qsvh265dec"] = "300";
  pluginRank["qsvh264dec"] = "300";
  pluginRank["qsvh265enc"] = "300";
  pluginRank["qsvh264enc"] = "300";
  pluginRank["amfh264dec"] = "300";
  pluginRank["amfh265dec"] = "300";
  pluginRank["amfhvp9dec"] = "300";
  pluginRank["amfhav1dec"] = "300";
  pluginRank["nvh264dec"] = "257";
  pluginRank["nvh265dec"] = "257";
  pluginRank["nvh264enc"] = "257";
  pluginRank["nvh265enc"] = "257";
  pluginRank["nvvp9dec"] = "257";
  pluginRank["nvvp9enc"] = "257";
  pluginRank["nvmpeg4videodec"] = "257";
  pluginRank["nvmpeg2videodec"] = "257";
  pluginRank["nvmpegvideodec"] = "257";
  pluginRank["mpph264enc"] = "256";
  pluginRank["mpph265enc"] = "256";
  pluginRank["mppvp8enc"] = "256";
  pluginRank["mppjpegenc"] = "256";
  pluginRank["mppvideodec"] = "256";
  pluginRank["mppjpegdec"] = "256";
  gstreamer["plugin_rank"] = pluginRank;

  config_json_["gstreamer"] = gstreamer;

  // system
  Json::Value system(Json::objectValue);

  // web_server
  // bind_mode: "local" = 127.0.0.1 (chỉ thiết bị local), "public" = 0.0.0.0 (chấp nhận từ mọi mạng)
  Json::Value webServer(Json::objectValue);
  webServer["enabled"] = true;
  webServer["ip_address"] = "0.0.0.0";
  webServer["bind_mode"] = "public";  // "local" | "public" (dùng khi không set ip_address)
  webServer["port"] = 8080;
  webServer["name"] = "default";
  webServer["max_body_size"] = 524288000; // 500MB
  webServer["max_memory_body_size"] = 104857600; // 100MB
  webServer["keepalive_requests"] = 100;
  webServer["keepalive_timeout"] = 60;
  webServer["reuse_port"] = true;
  Json::Value cors(Json::objectValue);
  cors["enabled"] = false;
  webServer["cors"] = cors;
  system["web_server"] = webServer;

  // performance
  Json::Value performance(Json::objectValue);
  performance["thread_num"] = 0; // 0 = auto-detect
  performance["min_threads"] = 16;
  performance["max_threads"] = 64;
  system["performance"] = performance;

  // logging: enabled + log_level control all; api_enabled/instance_enabled/sdk_output_enabled per category
  Json::Value logging(Json::objectValue);
  logging["enabled"] = true;
  logging["log_level"] = "info";  // none, fatal, error, warning, info, debug, verbose
  logging["api_enabled"] = false;
  logging["instance_enabled"] = false;
  logging["sdk_output_enabled"] = false;
  logging["log_file"] = "logs/api.log";
  logging["max_log_file_size"] = 52428800;
  logging["max_log_files"] = 3;
  logging["log_dir"] = "./logs";
  logging["retention_days"] = 30;
  logging["max_disk_usage_percent"] = 85;
  logging["cleanup_interval_hours"] = 24;
  logging["log_paths_mode"] = "";
  logging["log_dir_production"] = "/opt/edgeos-api/logs";
  logging["log_dir_development"] = "./logs";
  logging["suspend_disk_percent"] = 95;
  logging["resume_disk_percent"] = 88;
  logging["max_log_file_size"] = 104857600;
  logging["cvedix_log_level"] = "warning";
  system["logging"] = logging;

  // monitoring
  Json::Value monitoring(Json::objectValue);
  monitoring["watchdog_check_interval_ms"] = 5000;
  monitoring["watchdog_timeout_ms"] = 30000;
  monitoring["health_monitor_interval_ms"] = 1000;
  monitoring["max_cpu_percent"] = 0;   // 0 = disabled; reject new instances when system CPU >= this
  monitoring["max_ram_percent"] = 0;   // 0 = disabled; reject new instances when system RAM >= this
  Json::Value deviceReport(Json::objectValue);
  deviceReport["enabled"] = false;
  deviceReport["server_url"] = "";
  deviceReport["device_id"] = "";
  deviceReport["device_type"] = "aibox";
  deviceReport["interval_sec"] = 300;  // 5 phút
  deviceReport["latitude"] = 0.0;
  deviceReport["longitude"] = 0.0;
  deviceReport["reachability_timeout_sec"] = 10;
  deviceReport["report_timeout_sec"] = 30;
  monitoring["device_report"] = deviceReport;
  system["monitoring"] = monitoring;

  // directories (empty = use auto-detection with fallback)
  Json::Value directories(Json::objectValue);
  directories["instances_dir"] = "";
  directories["models_dir"] = "";
  directories["solutions_dir"] = "";
  directories["videos_dir"] = "";
  directories["fonts_dir"] = "";
  directories["nodes_dir"] = "";
  system["directories"] = directories;

  system["max_running_instances"] = 0; // 0 = unlimited
  system["modelforge_permissive"] = false;
  system["auto_reload"] = false; // Default: disable auto reload

  config_json_["system"] = system;
}

bool SystemConfig::validateConfig(const Json::Value &json) const {
  // Basic validation - check if it's an object
  if (!json.isObject()) {
    return false;
  }

  // Check for required top-level keys (optional validation)
  // We allow partial configs, so we don't require all keys

  return true;
}

int SystemConfig::getMaxRunningInstances() const {
  std::lock_guard<std::mutex> lock(mutex_);

  int maxInstances = 0; // Default: unlimited
  bool hasConfigJson = false;

  // Priority 1: Load from config.json (highest priority)
  if (config_json_.isMember("system") &&
      config_json_["system"].isMember("max_running_instances") &&
      config_json_["system"]["max_running_instances"].isInt()) {
    maxInstances = config_json_["system"]["max_running_instances"].asInt();
    hasConfigJson = true;
  }

  // Priority 2: Fallback to environment variables (only if config.json doesn't
  // have value) Check MAX_RUNNING_INSTANCES (fallback for
  // max_running_instances)
  if (!hasConfigJson) {
    const char *max_instances = std::getenv("MAX_RUNNING_INSTANCES");
    if (max_instances && strlen(max_instances) > 0) {
      try {
        int env_value = std::stoi(max_instances);
        if (env_value >= 0) {
          maxInstances = env_value;
        }
      } catch (...) {
        // Invalid value, keep default
      }
    }
  }

  return maxInstances;
}

void SystemConfig::setMaxRunningInstances(int maxInstances) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!config_json_.isMember("system")) {
    config_json_["system"] = Json::Value(Json::objectValue);
  }

  config_json_["system"]["max_running_instances"] = maxInstances;
}

std::vector<std::string> SystemConfig::getAutoDeviceList() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;

  if (config_json_.isMember("auto_device_list") &&
      config_json_["auto_device_list"].isArray()) {
    for (const auto &item : config_json_["auto_device_list"]) {
      if (item.isString()) {
        result.push_back(item.asString());
      }
    }
  }

  // When missing or empty, return GPU-first default so inference prefers GPU over CPU
  if (result.empty()) {
    result = getDefaultAutoDeviceList();
  }
  return result;
}

void SystemConfig::setAutoDeviceList(const std::vector<std::string> &devices) {
  std::lock_guard<std::mutex> lock(mutex_);

  Json::Value array(Json::arrayValue);
  for (const auto &device : devices) {
    array.append(device);
  }
  config_json_["auto_device_list"] = array;
}

std::vector<std::string> SystemConfig::getDecoderPriorityList() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;

  if (config_json_.isMember("decoder_priority_list") &&
      config_json_["decoder_priority_list"].isArray()) {
    for (const auto &item : config_json_["decoder_priority_list"]) {
      if (item.isString()) {
        result.push_back(item.asString());
      }
    }
  }

  return result;
}

void SystemConfig::setDecoderPriorityList(
    const std::vector<std::string> &decoders) {
  std::lock_guard<std::mutex> lock(mutex_);

  Json::Value array(Json::arrayValue);
  for (const auto &decoder : decoders) {
    array.append(decoder);
  }
  config_json_["decoder_priority_list"] = array;
}

SystemConfig::WebServerConfig SystemConfig::getWebServerConfig() const {
  std::lock_guard<std::mutex> lock(mutex_);
  WebServerConfig config;

  // Priority 1: Load from config.json (highest priority)
  bool hasConfigJson = false;
  if (config_json_.isMember("system") &&
      config_json_["system"].isMember("web_server")) {
    const auto &ws = config_json_["system"]["web_server"];
    hasConfigJson = true;

    if (ws.isMember("enabled") && ws["enabled"].isBool()) {
      config.enabled = ws["enabled"].asBool();
    }
    if (ws.isMember("ip_address") && ws["ip_address"].isString()) {
      config.ipAddress = ws["ip_address"].asString();
    }
    // bind_mode: "local" = 127.0.0.1 (chỉ mạng nội bộ), "public" = 0.0.0.0 (chấp nhận mọi kết nối)
    // Chỉ áp dụng khi ip_address chưa được set hoặc rỗng
    if ((!ws.isMember("ip_address") || config.ipAddress.empty()) &&
        ws.isMember("bind_mode") && ws["bind_mode"].isString()) {
      std::string mode = ws["bind_mode"].asString();
      if (mode == "local") {
        config.ipAddress = "127.0.0.1";
      } else if (mode == "public") {
        config.ipAddress = "0.0.0.0";
      }
    }
    if (ws.isMember("port") && ws["port"].isInt()) {
      config.port = static_cast<uint16_t>(ws["port"].asInt());
    }
    if (ws.isMember("name") && ws["name"].isString()) {
      config.name = ws["name"].asString();
    }
    if (ws.isMember("cors") && ws["cors"].isMember("enabled") &&
        ws["cors"]["enabled"].isBool()) {
      config.corsEnabled = ws["cors"]["enabled"].asBool();
    }
    if (ws.isMember("max_body_size") && ws["max_body_size"].isInt()) {
      config.maxBodySize = static_cast<size_t>(ws["max_body_size"].asInt());
    }
    if (ws.isMember("max_memory_body_size") && ws["max_memory_body_size"].isInt()) {
      config.maxMemoryBodySize = static_cast<size_t>(ws["max_memory_body_size"].asInt());
    }
    if (ws.isMember("keepalive_requests") && ws["keepalive_requests"].isInt()) {
      config.keepaliveRequests = static_cast<size_t>(ws["keepalive_requests"].asInt());
    }
    if (ws.isMember("keepalive_timeout") && ws["keepalive_timeout"].isInt()) {
      config.keepaliveTimeout = static_cast<size_t>(ws["keepalive_timeout"].asInt());
    }
    if (ws.isMember("reuse_port") && ws["reuse_port"].isBool()) {
      config.reusePort = ws["reuse_port"].asBool();
    }
  }

  // Priority 2: Fallback to environment variables (only if config.json doesn't
  // have value) Check API_HOST (fallback for ip_address)
  if (!hasConfigJson || config.ipAddress.empty()) {
    const char *api_host = std::getenv("API_HOST");
    if (api_host && strlen(api_host) > 0) {
      config.ipAddress = std::string(api_host);
    }
  }

  // Check API_PORT (fallback for port)
  if (!hasConfigJson || config.port == 0) {
    const char *api_port = std::getenv("API_PORT");
    if (api_port && strlen(api_port) > 0) {
      try {
        int port_int = std::stoi(api_port);
        if (port_int >= 1 && port_int <= 65535) {
          config.port = static_cast<uint16_t>(port_int);
        }
      } catch (...) {
        // Invalid port, keep existing value
      }
    }
  }

  // Fallback for performance settings from env vars
  if (!hasConfigJson) {
    config.maxBodySize = EnvConfig::getSizeT("CLIENT_MAX_BODY_SIZE", config.maxBodySize);
    config.maxMemoryBodySize = EnvConfig::getSizeT("CLIENT_MAX_MEMORY_BODY_SIZE", config.maxMemoryBodySize);
    config.keepaliveRequests = EnvConfig::getSizeT("KEEPALIVE_REQUESTS", config.keepaliveRequests);
    config.keepaliveTimeout = EnvConfig::getSizeT("KEEPALIVE_TIMEOUT", config.keepaliveTimeout);
    config.reusePort = EnvConfig::getBool("ENABLE_REUSE_PORT", config.reusePort);
  }

  return config;
}

void SystemConfig::setWebServerConfig(const WebServerConfig &config) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!config_json_.isMember("system")) {
    config_json_["system"] = Json::Value(Json::objectValue);
  }

  Json::Value webServer(Json::objectValue);
  webServer["enabled"] = config.enabled;
  webServer["ip_address"] = config.ipAddress;
  if (config.ipAddress == "127.0.0.1") {
    webServer["bind_mode"] = "local";
  } else if (config.ipAddress == "0.0.0.0") {
    webServer["bind_mode"] = "public";
  }
  webServer["port"] = config.port;
  webServer["name"] = config.name;
  webServer["max_body_size"] = static_cast<Json::Int64>(config.maxBodySize);
  webServer["max_memory_body_size"] = static_cast<Json::Int64>(config.maxMemoryBodySize);
  webServer["keepalive_requests"] = static_cast<Json::Int64>(config.keepaliveRequests);
  webServer["keepalive_timeout"] = static_cast<Json::Int64>(config.keepaliveTimeout);
  webServer["reuse_port"] = config.reusePort;

  Json::Value cors(Json::objectValue);
  cors["enabled"] = config.corsEnabled;
  webServer["cors"] = cors;

  config_json_["system"]["web_server"] = webServer;
}

void SystemConfig::initializeCurrentServerConfig(
    const WebServerConfig &config) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_server_config_ = config;
  server_config_initialized_ = true;
}

bool SystemConfig::hasWebServerConfigChanged(
    const WebServerConfig &newConfig) const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!server_config_initialized_) {
    return false; // Server config not initialized yet
  }

  // Check if port or host changed (these require server restart)
  return (current_server_config_.port != newConfig.port) ||
         (current_server_config_.ipAddress != newConfig.ipAddress);
}

SystemConfig::LoggingConfig SystemConfig::getLoggingConfig() const {
  std::lock_guard<std::mutex> lock(mutex_);
  LoggingConfig config;

  // Priority 1: Load from config.json (highest priority)
  bool hasConfigJson = false;
  bool hasLogLevel = false;
  if (config_json_.isMember("system") &&
      config_json_["system"].isMember("logging")) {
    const auto &log = config_json_["system"]["logging"];
    hasConfigJson = true;

    if (log.isMember("enabled") && log["enabled"].isBool()) {
      config.enabled = log["enabled"].asBool();
    }
    if (log.isMember("log_level") && log["log_level"].isString()) {
      config.logLevel = log["log_level"].asString();
      hasLogLevel = true;
    }
    if (log.isMember("api_enabled") && log["api_enabled"].isBool()) {
      config.apiEnabled = log["api_enabled"].asBool();
    }
    if (log.isMember("instance_enabled") && log["instance_enabled"].isBool()) {
      config.instanceEnabled = log["instance_enabled"].asBool();
    }
    if (log.isMember("sdk_output_enabled") && log["sdk_output_enabled"].isBool()) {
      config.sdkOutputEnabled = log["sdk_output_enabled"].asBool();
    }
    if (log.isMember("log_file") && log["log_file"].isString()) {
      config.logFile = log["log_file"].asString();
    }
    if (log.isMember("max_log_file_size") && log["max_log_file_size"].isInt()) {
      config.maxLogFileSize =
          static_cast<size_t>(log["max_log_file_size"].asInt());
    }
    if (log.isMember("max_log_files") && log["max_log_files"].isInt()) {
      config.maxLogFiles = log["max_log_files"].asInt();
    }
    if (log.isMember("log_dir") && log["log_dir"].isString()) {
      config.logDir = log["log_dir"].asString();
    }
    if (log.isMember("retention_days") && log["retention_days"].isInt()) {
      config.retentionDays = log["retention_days"].asInt();
    }
    if (log.isMember("max_disk_usage_percent") && log["max_disk_usage_percent"].isInt()) {
      config.maxDiskUsagePercent = log["max_disk_usage_percent"].asInt();
    }
    if (log.isMember("cleanup_interval_hours") && log["cleanup_interval_hours"].isInt()) {
      config.cleanupIntervalHours = log["cleanup_interval_hours"].asInt();
    }
    if (log.isMember("log_paths_mode") && log["log_paths_mode"].isString()) {
      config.logPathsMode = log["log_paths_mode"].asString();
    }
    if (log.isMember("log_dir_production") &&
        log["log_dir_production"].isString()) {
      config.logDirProduction = log["log_dir_production"].asString();
    }
    if (log.isMember("log_dir_development") &&
        log["log_dir_development"].isString()) {
      config.logDirDevelopment = log["log_dir_development"].asString();
    }
    if (log.isMember("suspend_disk_percent") &&
        log["suspend_disk_percent"].isInt()) {
      config.suspendDiskPercent = log["suspend_disk_percent"].asInt();
    }
    if (log.isMember("resume_disk_percent") &&
        log["resume_disk_percent"].isInt()) {
      config.resumeDiskPercent = log["resume_disk_percent"].asInt();
    }
    if (log.isMember("cvedix_log_level") &&
        log["cvedix_log_level"].isString()) {
      config.cvedixLogLevel = log["cvedix_log_level"].asString();
    }
  }

  // Priority 2: Fallback to environment variables (only if config.json doesn't
  // have value) Check LOG_LEVEL (fallback for log_level)
  if (!hasLogLevel) {
    const char *log_level = std::getenv("LOG_LEVEL");
    if (log_level && strlen(log_level) > 0) {
      config.logLevel = std::string(log_level);
    }
  }
  
  // Fallback for log_dir from env var
  if (!hasConfigJson || config.logDir.empty()) {
    const char *log_dir = std::getenv("LOG_DIR");
    if (log_dir && strlen(log_dir) > 0) {
      config.logDir = std::string(log_dir);
    }
  }

  // Override log_file directory if LOG_DIR is set and log_file is relative
  if (!config.logDir.empty()) {
    if (!std::filesystem::path(config.logFile).is_absolute()) {
      config.logFile =
          config.logDir + "/" +
          std::filesystem::path(config.logFile).filename().string();
    }
  }

  return config;
}

std::string SystemConfig::resolveLogBaseDirectory(const char *argv0) const {
  LoggingConfig c = getLoggingConfig();
  const char *env_ld = std::getenv("LOG_DIR");
  if (env_ld && env_ld[0]) {
    return std::string(env_ld);
  }
  std::string mode = c.logPathsMode;
  std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
  if (mode == "auto") {
    std::string p = argv0 ? argv0 : "";
    if (p.find("/opt/edgeos-api") != std::string::npos) {
      return c.logDirProduction;
    }
    return c.logDirDevelopment;
  }
  if (mode == "production") {
    return c.logDirProduction;
  }
  if (mode == "development") {
    return c.logDirDevelopment;
  }
  if (!c.logDir.empty()) {
    return c.logDir;
  }
  return "./logs";
}

void SystemConfig::setLoggingConfig(const LoggingConfig &config) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!config_json_.isMember("system")) {
    config_json_["system"] = Json::Value(Json::objectValue);
  }

  Json::Value logging(Json::objectValue);
  logging["enabled"] = config.enabled;
  logging["log_level"] = config.logLevel;
  logging["api_enabled"] = config.apiEnabled;
  logging["instance_enabled"] = config.instanceEnabled;
  logging["sdk_output_enabled"] = config.sdkOutputEnabled;
  logging["log_file"] = config.logFile;
  logging["max_log_file_size"] =
      static_cast<Json::Int64>(config.maxLogFileSize);
  logging["max_log_files"] = config.maxLogFiles;
  logging["log_dir"] = config.logDir;
  logging["retention_days"] = config.retentionDays;
  logging["max_disk_usage_percent"] = config.maxDiskUsagePercent;
  logging["cleanup_interval_hours"] = config.cleanupIntervalHours;
  logging["log_paths_mode"] = config.logPathsMode;
  logging["log_dir_production"] = config.logDirProduction;
  logging["log_dir_development"] = config.logDirDevelopment;
  logging["suspend_disk_percent"] = config.suspendDiskPercent;
  logging["resume_disk_percent"] = config.resumeDiskPercent;
  logging["cvedix_log_level"] = config.cvedixLogLevel;

  config_json_["system"]["logging"] = logging;
}

SystemConfig::PerformanceConfig SystemConfig::getPerformanceConfig() const {
  std::lock_guard<std::mutex> lock(mutex_);
  PerformanceConfig config;

  // Priority 1: Load from config.json
  if (config_json_.isMember("system") &&
      config_json_["system"].isMember("performance")) {
    const auto &perf = config_json_["system"]["performance"];
    
    if (perf.isMember("thread_num") && perf["thread_num"].isInt()) {
      config.threadNum = perf["thread_num"].asInt();
    }
    if (perf.isMember("min_threads") && perf["min_threads"].isInt()) {
      config.minThreads = static_cast<unsigned int>(perf["min_threads"].asInt());
    }
    if (perf.isMember("max_threads") && perf["max_threads"].isInt()) {
      config.maxThreads = static_cast<unsigned int>(perf["max_threads"].asInt());
    }
  }

  // Priority 2: Fallback to environment variable (explicit thread count)
  if (config.threadNum == 0) {
    config.threadNum = EnvConfig::getInt("THREAD_NUM", 0, 0, 256);
  }

  return config;
}

SystemConfig::MonitoringConfig SystemConfig::getMonitoringConfig() const {
  std::lock_guard<std::mutex> lock(mutex_);
  MonitoringConfig config;

  // Priority 1: Load from config.json (highest priority)
  bool hasConfigJson = false;
  if (config_json_.isMember("system") &&
      config_json_["system"].isMember("monitoring")) {
    const auto &mon = config_json_["system"]["monitoring"];
    hasConfigJson = true;
    
    if (mon.isMember("watchdog_check_interval_ms") && mon["watchdog_check_interval_ms"].isInt()) {
      config.watchdogCheckIntervalMs = static_cast<uint32_t>(mon["watchdog_check_interval_ms"].asInt());
    }
    if (mon.isMember("watchdog_timeout_ms") && mon["watchdog_timeout_ms"].isInt()) {
      config.watchdogTimeoutMs = static_cast<uint32_t>(mon["watchdog_timeout_ms"].asInt());
    }
    if (mon.isMember("health_monitor_interval_ms") && mon["health_monitor_interval_ms"].isInt()) {
      config.healthMonitorIntervalMs = static_cast<uint32_t>(mon["health_monitor_interval_ms"].asInt());
    }
    if (mon.isMember("max_cpu_percent") && mon["max_cpu_percent"].isInt()) {
      int v = mon["max_cpu_percent"].asInt();
      config.maxCpuPercent = (v >= 0 && v <= 100) ? v : 0;
    }
    if (mon.isMember("max_ram_percent") && mon["max_ram_percent"].isInt()) {
      int v = mon["max_ram_percent"].asInt();
      config.maxRamPercent = (v >= 0 && v <= 100) ? v : 0;
    }
  }

  // Priority 2: Fallback to environment variables (only if config.json doesn't have value)
  if (!hasConfigJson) {
    config.watchdogCheckIntervalMs = EnvConfig::getUInt32("WATCHDOG_CHECK_INTERVAL_MS", config.watchdogCheckIntervalMs);
    config.watchdogTimeoutMs = EnvConfig::getUInt32("WATCHDOG_TIMEOUT_MS", config.watchdogTimeoutMs);
    config.healthMonitorIntervalMs = EnvConfig::getUInt32("HEALTH_MONITOR_INTERVAL_MS", config.healthMonitorIntervalMs);
  }

  return config;
}

SystemConfig::DeviceReportConfig SystemConfig::getDeviceReportConfig() const {
  std::lock_guard<std::mutex> lock(mutex_);
  DeviceReportConfig config;
  bool hasConfigJson = false;
  if (config_json_.isMember("system") &&
      config_json_["system"].isMember("monitoring")) {
    const auto &mon = config_json_["system"]["monitoring"];
    if (mon.isMember("device_report")) {
      const auto &dr = mon["device_report"];
      hasConfigJson = true;
      if (dr.isMember("enabled") && dr["enabled"].isBool())
        config.enabled = dr["enabled"].asBool();
      if (dr.isMember("server_url") && dr["server_url"].isString())
        config.serverUrl = dr["server_url"].asString();
      if (dr.isMember("device_id") && dr["device_id"].isString())
        config.deviceId = dr["device_id"].asString();
      if (dr.isMember("device_type") && dr["device_type"].isString())
        config.deviceType = dr["device_type"].asString();
      if (dr.isMember("interval_sec") && dr["interval_sec"].isInt())
        config.intervalSec = static_cast<uint32_t>(dr["interval_sec"].asInt());
      if (dr.isMember("latitude") && dr["latitude"].isDouble())
        config.latitude = dr["latitude"].asDouble();
      if (dr.isMember("longitude") && dr["longitude"].isDouble())
        config.longitude = dr["longitude"].asDouble();
      if (dr.isMember("reachability_timeout_sec") && dr["reachability_timeout_sec"].isInt())
        config.reachabilityTimeoutSec = static_cast<uint32_t>(dr["reachability_timeout_sec"].asInt());
      if (dr.isMember("report_timeout_sec") && dr["report_timeout_sec"].isInt())
        config.reportTimeoutSec = static_cast<uint32_t>(dr["report_timeout_sec"].asInt());
    }
  }
  if (!hasConfigJson) {
    config.enabled = EnvConfig::getBool("DEVICE_REPORT_ENABLED", false);
    config.serverUrl = EnvConfig::getString("DEVICE_REPORT_SERVER_URL", "");
    config.deviceId = EnvConfig::getString("DEVICE_REPORT_DEVICE_ID", "");
    config.deviceType = EnvConfig::getString("DEVICE_REPORT_DEVICE_TYPE", "aibox");
    config.intervalSec = EnvConfig::getUInt32("DEVICE_REPORT_INTERVAL_SEC", 300);
    config.latitude = EnvConfig::getDouble("DEVICE_REPORT_LATITUDE", 0.0);
    config.longitude = EnvConfig::getDouble("DEVICE_REPORT_LONGITUDE", 0.0);
  }
  return config;
}

bool SystemConfig::getModelforgePermissive() const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (config_json_.isMember("system") &&
      config_json_["system"].isMember("modelforge_permissive") &&
      config_json_["system"]["modelforge_permissive"].isBool()) {
    return config_json_["system"]["modelforge_permissive"].asBool();
  }

  return false;
}

void SystemConfig::setModelforgePermissive(bool permissive) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!config_json_.isMember("system")) {
    config_json_["system"] = Json::Value(Json::objectValue);
  }

  config_json_["system"]["modelforge_permissive"] = permissive;
}

bool SystemConfig::getAutoReload() const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (config_json_.isMember("system") &&
      config_json_["system"].isMember("auto_reload") &&
      config_json_["system"]["auto_reload"].isBool()) {
    return config_json_["system"]["auto_reload"].asBool();
  }

  return false; // Default: disabled
}

void SystemConfig::setAutoReload(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!config_json_.isMember("system")) {
    config_json_["system"] = Json::Value(Json::objectValue);
  }

  config_json_["system"]["auto_reload"] = enabled;
}

std::string
SystemConfig::getGStreamerPipeline(const std::string &platform) const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (config_json_.isMember("gstreamer") &&
      config_json_["gstreamer"].isMember("decode_pipelines") &&
      config_json_["gstreamer"]["decode_pipelines"].isMember(platform) &&
      config_json_["gstreamer"]["decode_pipelines"][platform].isMember(
          "pipeline") &&
      config_json_["gstreamer"]["decode_pipelines"][platform]["pipeline"]
          .isString()) {
    return config_json_["gstreamer"]["decode_pipelines"][platform]["pipeline"]
        .asString();
  }

  return "";
}

void SystemConfig::setGStreamerPipeline(const std::string &platform,
                                        const std::string &pipeline) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!config_json_.isMember("gstreamer")) {
    config_json_["gstreamer"] = Json::Value(Json::objectValue);
  }
  if (!config_json_["gstreamer"].isMember("decode_pipelines")) {
    config_json_["gstreamer"]["decode_pipelines"] =
        Json::Value(Json::objectValue);
  }
  if (!config_json_["gstreamer"]["decode_pipelines"].isMember(platform)) {
    config_json_["gstreamer"]["decode_pipelines"][platform] =
        Json::Value(Json::objectValue);
  }

  config_json_["gstreamer"]["decode_pipelines"][platform]["pipeline"] =
      pipeline;
}

std::vector<std::string>
SystemConfig::getGStreamerCapabilities(const std::string &platform) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;

  if (config_json_.isMember("gstreamer") &&
      config_json_["gstreamer"].isMember("decode_pipelines") &&
      config_json_["gstreamer"]["decode_pipelines"].isMember(platform) &&
      config_json_["gstreamer"]["decode_pipelines"][platform].isMember(
          "capabilities") &&
      config_json_["gstreamer"]["decode_pipelines"][platform]["capabilities"]
          .isArray()) {
    for (const auto &cap : config_json_["gstreamer"]["decode_pipelines"]
                                       [platform]["capabilities"]) {
      if (cap.isString()) {
        result.push_back(cap.asString());
      }
    }
  }

  return result;
}

void SystemConfig::setGStreamerCapabilities(
    const std::string &platform, const std::vector<std::string> &capabilities) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!config_json_.isMember("gstreamer")) {
    config_json_["gstreamer"] = Json::Value(Json::objectValue);
  }
  if (!config_json_["gstreamer"].isMember("decode_pipelines")) {
    config_json_["gstreamer"]["decode_pipelines"] =
        Json::Value(Json::objectValue);
  }
  if (!config_json_["gstreamer"]["decode_pipelines"].isMember(platform)) {
    config_json_["gstreamer"]["decode_pipelines"][platform] =
        Json::Value(Json::objectValue);
  }

  Json::Value caps(Json::arrayValue);
  for (const auto &cap : capabilities) {
    caps.append(cap);
  }
  config_json_["gstreamer"]["decode_pipelines"][platform]["capabilities"] =
      caps;
}

std::string
SystemConfig::getGStreamerPluginRank(const std::string &pluginName) const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (config_json_.isMember("gstreamer") &&
      config_json_["gstreamer"].isMember("plugin_rank") &&
      config_json_["gstreamer"]["plugin_rank"].isMember(pluginName) &&
      config_json_["gstreamer"]["plugin_rank"][pluginName].isString()) {
    return config_json_["gstreamer"]["plugin_rank"][pluginName].asString();
  }

  return "";
}

void SystemConfig::setGStreamerPluginRank(const std::string &pluginName,
                                          const std::string &rank) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!config_json_.isMember("gstreamer")) {
    config_json_["gstreamer"] = Json::Value(Json::objectValue);
  }
  if (!config_json_["gstreamer"].isMember("plugin_rank")) {
    config_json_["gstreamer"]["plugin_rank"] = Json::Value(Json::objectValue);
  }

  config_json_["gstreamer"]["plugin_rank"][pluginName] = rank;
}

Json::Value SystemConfig::getConfigJson() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_json_;
}

Json::Value SystemConfig::getConfigSection(const std::string &path) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto [parent, key] = parsePath(path);
  if (!parent || !parent->isMember(key)) {
    return Json::Value(Json::nullValue);
  }

  return (*parent)[key];
}

bool SystemConfig::updateConfig(const Json::Value &json) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!json.isObject()) {
    return false;
  }

  // Merge JSON objects recursively
  for (const auto &key : json.getMemberNames()) {
    config_json_[key] = json[key];
  }

  return true;
}

bool SystemConfig::replaceConfig(const Json::Value &json) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!validateConfig(json)) {
    return false;
  }

  config_json_ = json;
  return true;
}

bool SystemConfig::updateConfigSection(const std::string &path,
                                       const Json::Value &value) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto [parent, key] = parsePath(path);
  if (!parent) {
    return false;
  }

  (*parent)[key] = value;
  return true;
}

bool SystemConfig::deleteConfigSection(const std::string &path) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto [parent, key] = parsePath(path);
  if (!parent || !parent->isMember(key)) {
    return false;
  }

  parent->removeMember(key);
  return true;
}

std::string SystemConfig::getConfigPath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_path_;
}

bool SystemConfig::reloadConfig() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (config_path_.empty()) {
    return false;
  }

  // Release lock before calling loadConfig (which will acquire it)
  std::string path = config_path_;
  mutex_.unlock();

  bool result = loadConfig(path);

  mutex_.lock();
  return result;
}

bool SystemConfig::resetToDefaults() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Initialize default configuration
  initializeDefaults();
  loaded_ = true;

  // Save to file if config path is set
  if (!config_path_.empty()) {
    std::string path = config_path_;
    mutex_.unlock();

    bool saved = saveConfig(path);

    mutex_.lock();
    return saved;
  }

  return true;
}

bool SystemConfig::isLoaded() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return loaded_;
}

std::pair<Json::Value *, std::string>
SystemConfig::parsePath(const std::string &path) const {
  if (path.empty()) {
    return {nullptr, ""};
  }

  // Split path by dots or forward slashes (support both formats)
  std::vector<std::string> parts;
  std::stringstream ss(path);
  std::string item;

  // Check if path contains forward slashes
  bool useSlash = (path.find('/') != std::string::npos);
  char delimiter = useSlash ? '/' : '.';

  while (std::getline(ss, item, delimiter)) {
    if (!item.empty()) {
      parts.push_back(item);
    }
  }

  if (parts.empty()) {
    return {nullptr, ""};
  }

  // Navigate to parent
  Json::Value *current = const_cast<Json::Value *>(&config_json_);
  for (size_t i = 0; i < parts.size() - 1; ++i) {
    if (!current->isMember(parts[i]) || !(*current)[parts[i]].isObject()) {
      return {nullptr, ""};
    }
    current = &((*current)[parts[i]]);
  }

  return {current, parts.back()};
}
