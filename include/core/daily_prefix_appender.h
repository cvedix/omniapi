#pragma once

#include <fstream>
#include <mutex>
#include <plog/Appenders/IAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <string>

class DailyPrefixAppender : public plog::IAppender {
public:
  enum class Mode {
    ApiLines,
    SdkLines,
    GeneralLines
  };

  DailyPrefixAppender(std::string category_dir, Mode mode, size_t max_file_bytes);

  void write(const plog::Record &record) override;

private:
  std::string buildPath(const std::string &date, int part) const;
  bool matchesMode(const char *msg) const;

  std::string dir_;
  Mode mode_;
  size_t max_bytes_;
  std::mutex mu_;
  std::string open_date_;
  int part_index_ = 0;
  size_t bytes_in_part_ = 0;
  std::ofstream out_;
};
