#pragma once

#include <cstdint>
#include <string>

struct DeviceReportSnapshot {
  std::string hostname;
  std::string localIp;
  std::string publicIp;
  std::string macAddress;
  std::string osVersion;
  std::string kernelVersion;
  std::string cpuModel;
  int cpuCores = 0;
  double cpuUsage = 0.0;
  int memTotalMB = 0;
  int memAvailableMB = 0;
  double memUsage = 0.0;
  int diskTotalGB = 0;
  int diskUsedGB = 0;
  int diskAvailableGB = 0;
  double diskUsage = 0.0;
  int64_t uptimeSeconds = 0;
  double loadAvg = 0.0;
  int activeStreams = 0;
};

class DeviceReportCollector {
public:
  DeviceReportSnapshot collect();
  void setActiveStreams(int n) { active_streams_ = n; }

private:
  int active_streams_ = 0;
  std::string getHostname();
  std::string getLocalIp();
  std::string getPublicIp();
  std::string getMacAddress();
  std::string getOsVersion();
  std::string getKernelVersion();
  std::string getCpuModel();
  int getCpuCores();
  double getCpuUsage();
  void getMemInfo(int& totalMB, int& availableMB, double& usagePercent);
  void getDiskInfo(int& totalGB, int& usedGB, int& availableGB, double& usagePercent);
  int64_t getUptimeSeconds();
  double getLoadAvg();
};
