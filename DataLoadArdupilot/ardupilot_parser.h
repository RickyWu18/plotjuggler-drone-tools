#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

struct ApFieldDef
{
  char        fmt_char  = 0;
  int         byte_size = 0;
  bool        is_string = false;
  bool        is_array  = false;
  std::string label;
  char        unit_id = '?';
  char        mult_id = '?';
};

struct ApMessageDef
{
  uint8_t                 msg_type      = 0;
  uint8_t                 msg_len       = 0;
  std::string             name;
  std::vector<ApFieldDef> fields;
  int                     timestamp_idx = -1;
  int                     instance_idx  = -1;
};

struct ApFmtuPending
{
  char units[16]       = {};
  char multipliers[16] = {};
};

struct ApSeries
{
  std::vector<double> timestamps;
  std::vector<double> values;
  std::string unit;
};

using ApSeriesMap = std::unordered_map<std::string, ApSeries>;

struct ApMessageStats
{
  std::string name;
  uint64_t    count = 0;
};

struct ApParameter
{
  std::string name;
  double      value = 0.0;
};

struct ApEmbeddedFile
{
  std::string           name;
  std::vector<uint8_t>  data;
};

class ArdupilotParser
{
public:
  using ProgressCallback = std::function<bool(size_t pos, size_t total)>;

  explicit ArdupilotParser(const uint8_t* data, size_t length,
                           bool loadFiles = true,
                           ProgressCallback progressCb = nullptr);

  const ApSeriesMap&                    getSeriesMap()      const { return _series;        }
  const std::vector<ApMessageStats>&   getMessageStats()   const { return _stats;         }
  const std::vector<ApParameter>&      getParameters()     const { return _params;        }
  const std::vector<ApEmbeddedFile>&   getEmbeddedFiles()  const { return _embeddedFiles; }

private:
  void parse();

  static ApMessageDef buildMessageDef(const uint8_t* payload86);
  static int          fieldByteSize(char c);
  static bool         isStringField(char c);
  static bool         isArrayField(char c);
  static std::vector<std::string> splitLabels(const char* buf, int len);
  static double       float16ToDouble(uint16_t bits);

  void applyFmtu(ApMessageDef& def, const char* units16, const char* mults16);
  void applyPendingFmtu(uint8_t msg_type);

  void parseDataPacket(const uint8_t* payload, const ApMessageDef& def);
  void parseUnitPacket(const uint8_t* payload, const ApMessageDef& def);
  void parseMultPacket(const uint8_t* payload, const ApMessageDef& def);
  void parseFmtuPacket(const uint8_t* payload, const ApMessageDef& def);
  void parseFilePacket(const uint8_t* payload, const ApMessageDef& def);
  void assembleEmbeddedFiles();

  double decodeField(const uint8_t* payload, size_t& offset, char fmt_char);

  const uint8_t* _data      = nullptr;
  size_t         _length    = 0;
  bool           _loadFiles = true;
  ProgressCallback _progressCb;

  std::unordered_map<uint8_t, ApMessageDef>  _fmtTable;
  std::unordered_map<char,   std::string>    _unitTable;
  std::unordered_map<char,   double>         _multTable;
  std::unordered_map<uint8_t, ApFmtuPending> _pendingFmtu;

  uint8_t _unitMsgType = 0;
  uint8_t _multMsgType = 0;
  uint8_t _fmtuMsgType = 0;
  uint8_t _fileMsgType = 0;

  double _lastTimestamp = 0.0;

  ApSeriesMap                  _series;
  std::vector<ApMessageStats>  _stats;
  std::unordered_map<std::string, size_t> _statsIndex;
  std::vector<ApParameter>                _params;
  std::unordered_map<std::string, size_t> _paramsIndex;

  std::unordered_map<std::string,
      std::vector<std::pair<uint32_t, std::vector<uint8_t>>>> _fileChunks;
  std::vector<ApEmbeddedFile> _embeddedFiles;
};
