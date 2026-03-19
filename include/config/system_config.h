#pragma once

#include <filesystem>
#include <json/json.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief System Configuration Manager
 *
 * Manages system-wide configuration loaded from config.json
 * Thread-safe singleton pattern
 */
class SystemConfig {
public:
  /**
   * @brief Get singleton instance
   */
  static SystemConfig &getInstance();

  /**
   * @brief Load configuration from file
   * @param configPath Path to config.json file
   * @return true if loaded successfully
   */
  bool loadConfig(const std::string &configPath);

  /**
   * @brief Save configuration to file
   * @param configPath Path to config.json file (optional, uses current path if
   * empty)
   * @return true if saved successfully
   */
  bool saveConfig(const std::string &configPath = "");

  /**
   * @brief Get max running instances limit
   * @return Max instances (0 = unlimited)
   */
  int getMaxRunningInstances() const;

  /**
   * @brief Set max running instances limit
   * @param maxInstances Max instances (0 = unlimited)
   */
  void setMaxRunningInstances(int maxInstances);

  /**
   * @brief Get auto device list
   */
  std::vector<std::string> getAutoDeviceList() const;

  /**
   * @brief Set auto device list
   */
  void setAutoDeviceList(const std::vector<std::string> &devices);

  /**
   * @brief Get decoder priority list
   */
  std::vector<std::string> getDecoderPriorityList() const;

  /**
   * @brief Set decoder priority list
   */
  void setDecoderPriorityList(const std::vector<std::string> &decoders);

  /**
   * @brief Get web server configuration
   */
  struct WebServerConfig {
    bool enabled = true;
    std::string ipAddress = "0.0.0.0";
    uint16_t port = 8080;
    std::string name = "default";
    bool corsEnabled = false;
    size_t maxBodySize = 524288000; // 500MB
    size_t maxMemoryBodySize = 104857600; // 100MB
    size_t keepaliveRequests = 100;
    size_t keepaliveTimeout = 60;
    bool reusePort = true;
  };
  WebServerConfig getWebServerConfig() const;

  /**
   * @brief Set web server configuration
   */
  void setWebServerConfig(const WebServerConfig &config);

  /**
   * @brief Get performance configuration
   */
  struct PerformanceConfig {
    int threadNum = 0; // 0 = auto-detect
    unsigned int minThreads = 16;
    unsigned int maxThreads = 64;
  };
  PerformanceConfig getPerformanceConfig() const;

  /**
   * @brief Get monitoring configuration
   */
  struct MonitoringConfig {
    uint32_t watchdogCheckIntervalMs = 5000;
    uint32_t watchdogTimeoutMs = 30000;
    uint32_t healthMonitorIntervalMs = 1000;
  };
  MonitoringConfig getMonitoringConfig() const;

  /**
   * @brief Get logging configuration
   */
  struct LoggingConfig {
    std::string logFile = "logs/api.log";
    std::string logLevel = "debug";
    size_t maxLogFileSize = 104857600; // 100MB default
    int maxLogFiles = 3;
    std::string logDir = "./logs";
    int retentionDays = 30;
    int maxDiskUsagePercent = 85;
    int cleanupIntervalHours = 24;
    /** empty = legacy (use log_dir); auto | production | development */
    std::string logPathsMode;
    std::string logDirProduction = "/opt/edgeos-api/logs";
    std::string logDirDevelopment = "./logs";
    int suspendDiskPercent = 95;
    int resumeDiskPercent = 88;
    /** CVEDIX SDK console: error | warning | info | debug — default quiet */
    std::string cvedixLogLevel = "warning";
  };
  LoggingConfig getLoggingConfig() const;

  /** Effective log root: LOG_DIR env > log_paths_mode > log_dir */
  std::string resolveLogBaseDirectory(const char *argv0) const;

  /**
   * @brief Set logging configuration
   */
  void setLoggingConfig(const LoggingConfig &config);

  /**
   * @brief Get modelforge permissive flag
   */
  bool getModelforgePermissive() const;

  /**
   * @brief Set modelforge permissive flag
   */
  void setModelforgePermissive(bool permissive);

  /**
   * @brief Get auto reload flag (for server restart on config change)
   * @return true if auto reload is enabled
   */
  bool getAutoReload() const;

  /**
   * @brief Set auto reload flag
   * @param enabled Enable or disable auto reload
   */
  void setAutoReload(bool enabled);

  /**
   * @brief Get GStreamer decode pipeline for a platform
   * @param platform Platform name (auto, jetson, nvidia, msdk, vaapi)
   * @return Pipeline string if found, empty otherwise
   */
  std::string getGStreamerPipeline(const std::string &platform) const;

  /**
   * @brief Set GStreamer decode pipeline for a platform
   */
  void setGStreamerPipeline(const std::string &platform,
                            const std::string &pipeline);

  /**
   * @brief Get GStreamer capabilities for a platform
   * @param platform Platform name
   * @return Vector of capability strings
   */
  std::vector<std::string>
  getGStreamerCapabilities(const std::string &platform) const;

  /**
   * @brief Set GStreamer capabilities for a platform
   */
  void setGStreamerCapabilities(const std::string &platform,
                                const std::vector<std::string> &capabilities);

  /**
   * @brief Get GStreamer plugin rank
   * @param pluginName Plugin name
   * @return Rank string if found, empty otherwise
   */
  std::string getGStreamerPluginRank(const std::string &pluginName) const;

  /**
   * @brief Set GStreamer plugin rank
   */
  void setGStreamerPluginRank(const std::string &pluginName,
                              const std::string &rank);

  /**
   * @brief Get full configuration as JSON
   */
  Json::Value getConfigJson() const;

  /**
   * @brief Get configuration section as JSON
   * @param path JSON path (e.g., "system.web_server",
   * "gstreamer.decode_pipelines")
   * @return JSON value if found, null otherwise
   */
  Json::Value getConfigSection(const std::string &path) const;

  /**
   * @brief Update configuration from JSON (merge)
   * @param json JSON configuration to merge
   * @return true if updated successfully
   */
  bool updateConfig(const Json::Value &json);

  /**
   * @brief Replace entire configuration
   * @param json New configuration JSON
   * @return true if replaced successfully
   */
  bool replaceConfig(const Json::Value &json);

  /**
   * @brief Update configuration section
   * @param path JSON path (e.g., "system.web_server")
   * @param value New value for the section
   * @return true if updated successfully
   */
  bool updateConfigSection(const std::string &path, const Json::Value &value);

  /**
   * @brief Delete configuration section
   * @param path JSON path
   * @return true if deleted successfully
   */
  bool deleteConfigSection(const std::string &path);

  /**
   * @brief Get config file path
   */
  std::string getConfigPath() const;

  /**
   * @brief Reload configuration from file
   */
  bool reloadConfig();

  /**
   * @brief Reset configuration to default values
   * @return true if reset successfully
   */
  bool resetToDefaults();

  /**
   * @brief Check if configuration is loaded
   */
  bool isLoaded() const;

  /**
   * @brief Initialize current server configuration (called at server startup)
   * @param config Current web server configuration
   */
  void initializeCurrentServerConfig(const WebServerConfig &config);

  /**
   * @brief Check if web server config changed (port/host) and needs reload
   * @param newConfig New web server configuration
   * @return true if port or host changed
   */
  bool hasWebServerConfigChanged(const WebServerConfig &newConfig) const;

private:
  SystemConfig() = default;
  ~SystemConfig() = default;
  SystemConfig(const SystemConfig &) = delete;
  SystemConfig &operator=(const SystemConfig &) = delete;

  std::string config_path_;
  mutable std::mutex mutex_;
  Json::Value config_json_;
  bool loaded_ = false;

  // Current running server configuration (for auto-reload detection)
  WebServerConfig current_server_config_;
  bool server_config_initialized_ = false;

  /**
   * @brief Initialize default configuration
   */
  void initializeDefaults();

  /**
   * @brief Validate configuration structure
   */
  bool validateConfig(const Json::Value &json) const;

  /**
   * @brief Parse JSON path and get value
   */
  Json::Value *getJsonValueByPath(const std::string &path,
                                  bool createIfNotExists = false);

  /**
   * @brief Parse JSON path and get parent and key
   */
  std::pair<Json::Value *, std::string>
  parsePath(const std::string &path) const;
};
