#include "ardupilot_parser.h"

#include <cstring>
#include <algorithm>
#include <stdexcept>

static constexpr uint8_t HEAD_BYTE1     = 0xA3;
static constexpr uint8_t HEAD_BYTE2     = 0x95;
static constexpr uint8_t FMT_MSGID      = 128;
static constexpr int     FMT_PAYLOAD_LEN = 86;

ArdupilotParser::ArdupilotParser(const uint8_t* data, size_t length)
  : _data(data), _length(length)
{
  // Seed built-in multiplier table so scaling works before MULT packets arrive
  _multTable['?'] = 1.0;
  _multTable['-'] = 1.0;   // "no units" — treat as identity rather than zero
  _multTable['0'] = 1.0;
  _multTable['A'] = 1e-1;
  _multTable['B'] = 1e-2;
  _multTable['C'] = 1e-3;
  _multTable['D'] = 1e-4;
  _multTable['E'] = 1e-5;
  _multTable['F'] = 1e-6;
  _multTable['G'] = 1e-7;
  _multTable['I'] = 1e-9;
  _multTable['1'] = 1e1;
  _multTable['2'] = 1e2;
  _multTable['!'] = 3.6;
  _multTable['/'] = 3600.0;

  parse();
}

void ArdupilotParser::parse()
{
  size_t pos = 0;

  while (pos + 3 <= _length)
  {
    if (_data[pos] != HEAD_BYTE1 || _data[pos + 1] != HEAD_BYTE2)
    {
      pos++;
      continue;
    }

    const uint8_t msgid = _data[pos + 2];
    pos += 3;

    if (msgid == FMT_MSGID)
    {
      if (pos + static_cast<size_t>(FMT_PAYLOAD_LEN) > _length) break;

      ApMessageDef def = buildMessageDef(_data + pos);
      pos += static_cast<size_t>(FMT_PAYLOAD_LEN);

      _fmtTable[def.msg_type] = def;

      if      (def.name == "UNIT") _unitMsgType = def.msg_type;
      else if (def.name == "MULT") _multMsgType = def.msg_type;
      else if (def.name == "FMTU") _fmtuMsgType = def.msg_type;

      applyPendingFmtu(def.msg_type);
      continue;
    }

    auto it = _fmtTable.find(msgid);
    if (it == _fmtTable.end())
    {
      // Unknown message type — payload length unknown, cannot continue safely
      break;
    }

    const ApMessageDef& def = it->second;
    if (def.msg_len < 3) break;

    const size_t payload_len = static_cast<size_t>(def.msg_len) - 3;
    if (pos + payload_len > _length) break;

    const uint8_t* payload = _data + pos;
    pos += payload_len;

    if      (msgid == _unitMsgType) parseUnitPacket(payload, def);
    else if (msgid == _multMsgType) parseMultPacket(payload, def);
    else if (msgid == _fmtuMsgType) parseFmtuPacket(payload, def);
    else                            parseDataPacket(payload, def);
  }
}

ApMessageDef ArdupilotParser::buildMessageDef(const uint8_t* payload86)
{
  ApMessageDef def;
  def.msg_type = payload86[0];
  def.msg_len  = payload86[1];

  const char* name_buf = reinterpret_cast<const char*>(payload86 + 2);
  const size_t name_len = strnlen(name_buf, 4);
  def.name = std::string(name_buf, name_len);

  const char* fmt_buf    = reinterpret_cast<const char*>(payload86 + 6);
  const char* labels_buf = reinterpret_cast<const char*>(payload86 + 22);
  auto labels = splitLabels(labels_buf, 64);

  for (size_t i = 0; i < 16 && fmt_buf[i] != '\0'; i++)
  {
    const char c = fmt_buf[i];
    ApFieldDef field;
    field.fmt_char  = c;
    field.byte_size = fieldByteSize(c);
    field.is_string = isStringField(c);
    field.is_array  = isArrayField(c);
    if (i < labels.size()) field.label = labels[i];

    if (c == 'Q' && field.label == "TimeUS")
      def.timestamp_idx = static_cast<int>(i);

    if ((c == 'B' || c == 'M') &&
        (field.label == "I" || field.label == "Instance"))
      def.instance_idx = static_cast<int>(i);

    def.fields.push_back(std::move(field));
  }

  return def;
}

int ArdupilotParser::fieldByteSize(char c)
{
  switch (c)
  {
    case 'Q': case 'q': case 'd':              return 8;
    case 'f': case 'I': case 'i':
    case 'L': case 'e': case 'E':              return 4;
    case 'H': case 'h': case 'c':
    case 'C': case 'g':                        return 2;
    case 'B': case 'M': case 'b':              return 1;
    case 'n':                                  return 4;
    case 'N':                                  return 16;
    case 'Z':                                  return 64;
    case 'a':                                  return 64;
    default:                                   return 0;
  }
}

bool ArdupilotParser::isStringField(char c)
{
  return c == 'n' || c == 'N' || c == 'Z';
}

bool ArdupilotParser::isArrayField(char c)
{
  return c == 'a';
}

std::vector<std::string> ArdupilotParser::splitLabels(const char* buf, int len)
{
  std::vector<std::string> result;
  std::string token;
  for (int i = 0; i < len && buf[i] != '\0'; i++)
  {
    if (buf[i] == ',')
    {
      result.push_back(token);
      token.clear();
    }
    else
    {
      token += buf[i];
    }
  }
  if (!token.empty()) result.push_back(token);
  return result;
}

double ArdupilotParser::float16ToDouble(uint16_t bits)
{
  const uint32_t sign     = (bits >> 15) & 0x1;
  const uint32_t exponent = (bits >> 10) & 0x1F;
  const uint32_t mantissa =  bits        & 0x3FF;

  uint32_t f;
  if (exponent == 0)
  {
    if (mantissa == 0)
    {
      f = sign << 31;
    }
    else
    {
      uint32_t e = 1;
      uint32_t m = mantissa;
      while (!(m & 0x400)) { m <<= 1; e--; }
      m &= ~0x400u;
      f = (sign << 31) | ((e + 127 - 15) << 23) | (m << 13);
    }
  }
  else if (exponent == 31)
  {
    f = (sign << 31) | (0xFFu << 23) | (mantissa << 13);
  }
  else
  {
    f = (sign << 31) | ((exponent + 127 - 15) << 23) | (mantissa << 13);
  }

  float result;
  memcpy(&result, &f, 4);
  return static_cast<double>(result);
}

double ArdupilotParser::decodeField(const uint8_t* payload, size_t& offset, char fmt_char)
{
  double value = 0.0;
  switch (fmt_char)
  {
    case 'Q': { uint64_t v; memcpy(&v, payload + offset, 8); value = static_cast<double>(v); offset += 8; break; }
    case 'q': { int64_t  v; memcpy(&v, payload + offset, 8); value = static_cast<double>(v); offset += 8; break; }
    case 'd': { double   v; memcpy(&v, payload + offset, 8); value = v;                       offset += 8; break; }
    case 'f': { float    v; memcpy(&v, payload + offset, 4); value = static_cast<double>(v); offset += 4; break; }
    case 'I': { uint32_t v; memcpy(&v, payload + offset, 4); value = static_cast<double>(v); offset += 4; break; }
    case 'i': { int32_t  v; memcpy(&v, payload + offset, 4); value = static_cast<double>(v); offset += 4; break; }
    case 'L': { int32_t  v; memcpy(&v, payload + offset, 4); value = static_cast<double>(v); offset += 4; break; }
    case 'e': { int32_t  v; memcpy(&v, payload + offset, 4); value = static_cast<double>(v); offset += 4; break; }
    case 'E': { uint32_t v; memcpy(&v, payload + offset, 4); value = static_cast<double>(v); offset += 4; break; }
    case 'H': { uint16_t v; memcpy(&v, payload + offset, 2); value = static_cast<double>(v); offset += 2; break; }
    case 'h': { int16_t  v; memcpy(&v, payload + offset, 2); value = static_cast<double>(v); offset += 2; break; }
    case 'c': { int16_t  v; memcpy(&v, payload + offset, 2); value = static_cast<double>(v); offset += 2; break; }
    case 'C': { uint16_t v; memcpy(&v, payload + offset, 2); value = static_cast<double>(v); offset += 2; break; }
    case 'g': { uint16_t v; memcpy(&v, payload + offset, 2); value = float16ToDouble(v);     offset += 2; break; }
    case 'B': case 'M': { uint8_t v; memcpy(&v, payload + offset, 1); value = static_cast<double>(v); offset += 1; break; }
    case 'b': { int8_t   v; memcpy(&v, payload + offset, 1); value = static_cast<double>(v); offset += 1; break; }
    default: break;
  }
  return value;
}

void ArdupilotParser::parseDataPacket(const uint8_t* payload, const ApMessageDef& def)
{
  // Extract PARM name/value before the generic numeric pass skips string fields
  if (def.name == "PARM")
  {
    std::string parm_name;
    double      parm_value = 0.0;
    size_t      parm_offset = 0;
    for (const auto& field : def.fields)
    {
      if (field.label == "Name" && field.is_string)
      {
        const char* s = reinterpret_cast<const char*>(payload + parm_offset);
        parm_name = std::string(s, strnlen(s, static_cast<size_t>(field.byte_size)));
      }
      else if (field.label == "Value" && field.fmt_char == 'f')
      {
        float v; memcpy(&v, payload + parm_offset, 4);
        parm_value = static_cast<double>(v);
      }
      parm_offset += static_cast<size_t>(field.byte_size);
    }
    if (!parm_name.empty())
    {
      auto it = _paramsIndex.find(parm_name);
      if (it != _paramsIndex.end())
        _params[it->second].value = parm_value;
      else
      {
        _paramsIndex[parm_name] = _params.size();
        _params.push_back({parm_name, parm_value});
      }
    }
  }

  // Decode all numeric field values in a single pass
  size_t offset = 0;
  std::vector<double> values(def.fields.size(), 0.0);

  for (size_t i = 0; i < def.fields.size(); i++)
  {
    const auto& field = def.fields[i];
    if (field.is_string || field.is_array)
      offset += static_cast<size_t>(field.byte_size);
    else
      values[i] = decodeField(payload, offset, field.fmt_char);
  }

  // Extract timestamp from raw bytes to avoid floating-point precision loss
  double timestamp = _lastTimestamp;
  if (def.timestamp_idx >= 0 &&
      def.timestamp_idx < static_cast<int>(def.fields.size()))
  {
    size_t ts_offset = 0;
    for (int i = 0; i < def.timestamp_idx; i++)
      ts_offset += static_cast<size_t>(def.fields[i].byte_size);

    uint64_t ts_raw;
    memcpy(&ts_raw, payload + ts_offset, 8);
    timestamp = static_cast<double>(ts_raw) * 1e-6;
    _lastTimestamp = timestamp;
  }

  // Extract instance index for multi-sensor messages
  int instance = -1;
  if (def.instance_idx >= 0 &&
      def.instance_idx < static_cast<int>(def.fields.size()))
    instance = static_cast<int>(values[def.instance_idx]);

  const std::string group_prefix = (instance >= 0)
      ? def.name + "/" + std::to_string(instance)
      : def.name;

  // Emit numeric series
  for (size_t i = 0; i < def.fields.size(); i++)
  {
    const auto& field = def.fields[i];
    if (field.is_string || field.is_array) continue;

    double scaled = values[i];
    if (field.mult_id != '?')
    {
      auto mult_it = _multTable.find(field.mult_id);
      if (mult_it != _multTable.end())
        scaled *= mult_it->second;
    }
    else
    {
      // Fallback for older logs without FMTU packets
      switch (field.fmt_char)
      {
        case 'c': case 'C': scaled *= 1e-2; break;
        case 'e': case 'E': scaled *= 1e-4; break;
        case 'L':           scaled *= 1e-7; break;
        default:            break;
      }
    }

    const std::string key = group_prefix + "/" + field.label;
    auto& series = _series[key];
    if (series.unit.empty() && field.unit_id != '?')
    {
      auto unit_it = _unitTable.find(field.unit_id);
      if (unit_it != _unitTable.end() && !unit_it->second.empty())
        series.unit = unit_it->second;
    }
    series.timestamps.push_back(timestamp);
    series.values.push_back(scaled);
  }

  // Update message statistics
  auto stats_it = _statsIndex.find(def.name);
  if (stats_it != _statsIndex.end())
  {
    _stats[stats_it->second].count++;
  }
  else
  {
    _statsIndex[def.name] = _stats.size();
    _stats.push_back({def.name, 1});
  }
}

void ArdupilotParser::parseUnitPacket(const uint8_t* payload, const ApMessageDef& def)
{
  size_t offset = 0;
  char type_id = 0;
  std::string unit_str;

  for (const auto& field : def.fields)
  {
    if (field.label == "Id" && (field.fmt_char == 'b' || field.fmt_char == 'B'))
    {
      int8_t v; memcpy(&v, payload + offset, 1);
      type_id = static_cast<char>(v);
    }
    else if (field.label == "Label" && field.is_string)
    {
      const char* s = reinterpret_cast<const char*>(payload + offset);
      unit_str = std::string(s, strnlen(s, static_cast<size_t>(field.byte_size)));
    }
    offset += static_cast<size_t>(field.byte_size);
  }

  if (type_id != 0) _unitTable[type_id] = unit_str;
}

void ArdupilotParser::parseMultPacket(const uint8_t* payload, const ApMessageDef& def)
{
  size_t offset = 0;
  char type_id = 0;
  double mult_val = 1.0;

  for (const auto& field : def.fields)
  {
    if (field.label == "Id" && (field.fmt_char == 'b' || field.fmt_char == 'B'))
    {
      int8_t v; memcpy(&v, payload + offset, 1);
      type_id = static_cast<char>(v);
    }
    else if (field.label == "Mult" && field.fmt_char == 'd')
    {
      memcpy(&mult_val, payload + offset, 8);
    }
    offset += static_cast<size_t>(field.byte_size);
  }

  if (type_id != 0) _multTable[type_id] = mult_val;
}

void ArdupilotParser::parseFmtuPacket(const uint8_t* payload, const ApMessageDef& def)
{
  size_t offset = 0;
  uint8_t fmt_type = 0;
  char units[16] = {};
  char mults[16] = {};

  for (const auto& field : def.fields)
  {
    if (field.label == "FmtType" && (field.fmt_char == 'B' || field.fmt_char == 'b'))
    {
      memcpy(&fmt_type, payload + offset, 1);
    }
    else if (field.label == "UnitIds" && field.is_string)
    {
      const size_t copy_len = std::min(static_cast<size_t>(field.byte_size), size_t(16));
      memcpy(units, payload + offset, copy_len);
    }
    else if (field.label == "MultIds" && field.is_string)
    {
      const size_t copy_len = std::min(static_cast<size_t>(field.byte_size), size_t(16));
      memcpy(mults, payload + offset, copy_len);
    }
    offset += static_cast<size_t>(field.byte_size);
  }

  auto it = _fmtTable.find(fmt_type);
  if (it != _fmtTable.end())
  {
    applyFmtu(it->second, units, mults);
  }
  else
  {
    ApFmtuPending pending;
    memcpy(pending.units,       units, 16);
    memcpy(pending.multipliers, mults, 16);
    _pendingFmtu[fmt_type] = pending;
  }
}

void ArdupilotParser::applyFmtu(ApMessageDef& def, const char* units16, const char* mults16)
{
  const size_t n = std::min(def.fields.size(), size_t(16));
  for (size_t i = 0; i < n; i++)
  {
    if (units16[i] != '\0') def.fields[i].unit_id = units16[i];
    if (mults16[i] != '\0') def.fields[i].mult_id = mults16[i];
  }
}

void ArdupilotParser::applyPendingFmtu(uint8_t msg_type)
{
  auto it = _pendingFmtu.find(msg_type);
  if (it == _pendingFmtu.end()) return;

  applyFmtu(_fmtTable[msg_type], it->second.units, it->second.multipliers);
  _pendingFmtu.erase(it);
}
