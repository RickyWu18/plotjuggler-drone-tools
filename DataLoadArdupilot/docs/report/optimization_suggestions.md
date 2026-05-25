# ArduPilot Plugin — Optimization Suggestions

**Files:** `DataLoadArdupilot/ardupilot_parser.cpp`, `DataLoadArdupilot/dataload_ardupilot.cpp`  
**Date:** 2026-05-25

---

## Priority Overview

### `ardupilot_parser.cpp`

| # | Suggestion | Impact | Effort |
|---|-----------|--------|--------|
| P1 | Two-pass parse | High (correctness + order-independence) | Medium |
| P2 | Pre-build series keys at FMT time | High (eliminates hot-path allocations) | Low |
| P3 | Cache stats index on `ApMessageDef` | Medium (removes map lookup per packet) | Low |
| P4 | Skip `decodeField` for timestamp index | Low | Trivial |
| P5 | Reserve series vectors upfront | Low–Medium (depends on log size) | Low |
| P6 | Zero-alloc label splitting | Low (cold path) | Medium |

### `dataload_ardupilot.cpp`

| # | Suggestion | Impact | Effort |
|---|-----------|--------|--------|
| D1 | Remove redundant series_map counting pass | Low | Trivial |
| D2 | Throttle `setValue` / `processEvents` calls | Medium (UI responsiveness) | Trivial |
| D3 | Single-pass unit `/` replacement | Low | Trivial |
| D4 | RAII for `file.unmap` | Low (correctness/safety) | Low |
| D5 | Interleaved `{t, v}` layout for `ApSeries` | Medium (cache efficiency) | Medium |
| D6 | Avoid temporary strings for `display_key` | Low | Trivial |

---

## 1. Two-Pass Parse (High Impact — also fixes correctness bugs)

**Current problem:**  
UNIT / MULT / FMTU packets may arrive after the first data packets that need
them. This causes:
- Series permanently missing their unit string (audit issue #2).
- Multiplier miss going undetected (audit issue #1).

**Suggestion:**  
Do a lightweight first pass that skips all data payloads and only processes
`FMT`, `UNIT`, `MULT`, and `FMTU` packets. Once the tables are fully populated,
do the normal second pass to decode data.

```cpp
// Pass 1: build definition tables only
void ArdupilotParser::buildTables();

// Pass 2: decode data using fully-populated tables
void ArdupilotParser::decodeData();
```

The overhead is one extra linear scan of the file, which is cheap compared to
the data decode phase — most of the file bytes are skipped in pass 1.

**Benefit:** Eliminates the ordering dependency entirely; both audit issues #1
and #2 disappear.

---

## 2. Pre-Build Series Keys at FMT Registration Time (High Impact)

**Current problem:**  
Every data packet rebuilds the series key strings from scratch:

```cpp
// Inside the per-field loop — runs on every sample of every message
const std::string key = group_prefix + "/" + field.label;
auto& series = _series[key];
```

For a 10-minute log at 400 Hz with 20-field messages, this is ~4.8 million
string constructions and hash lookups.

**Suggestion:**  
At the time a FMT is registered (or when its FMTU is applied), build a
`std::vector<std::string> series_keys` on `ApMessageDef` — one key per field.
`parseDataPacket` then indexes directly into that pre-built list:

```cpp
// In ApMessageDef (ardupilot_parser.h)
std::vector<std::string> series_keys;  // populated after FMTU is applied

// In buildMessageDef / applyFmtu
def.series_keys[i] = def.name + "/" + field.label;

// In parseDataPacket — hot path
auto& series = _series[def.series_keys[i]];
```

Instance-based keys (`MSG/0/Field`) still need a runtime prefix, but only the
instance number varies; the suffix can still be pre-built per field.

**Benefit:** Eliminates all per-sample string allocation and most hash-map
overhead in the decode hot path.

---

## 3. Cache Stats Index on `ApMessageDef` (Medium Impact)

**Current problem:**  
At the end of every `parseDataPacket`, a string-keyed lookup is done to find
the stats counter:

```cpp
auto stats_it = _statsIndex.find(def.name);
```

This is a `std::unordered_map<std::string, size_t>` lookup on every packet.

**Suggestion:**  
Add a `int stats_idx = -1` field to `ApMessageDef`. Populate it when the first
data packet for a message type is processed (or when FMT is registered):

```cpp
// In ApMessageDef
int stats_idx = -1;

// In parseDataPacket, on first visit:
if (def.stats_idx < 0)
{
  def.stats_idx = static_cast<int>(_stats.size());
  _stats.push_back({def.name, 0});
}
_stats[def.stats_idx].count++;
```

**Benefit:** Replaces a map lookup + string hash with a direct array index on
every data packet.

---

## 4. Skip `decodeField` for the Timestamp Index (Low Impact)

**Current problem:**  
The timestamp field is decoded twice:

```cpp
// Pass 1 — general field loop: decodes to lossy double (result discarded)
values[def.timestamp_idx] = decodeField(payload, offset, 'Q');

// Pass 2 — explicit re-read for precision
uint64_t ts_raw;
memcpy(&ts_raw, payload + ts_offset, 8);
timestamp = static_cast<double>(ts_raw) * 1e-6;
```

The `values[timestamp_idx]` result is never used in the series-emit loop
(the `TimeUS` field is numeric but skipped as a "not a data series" field
only implicitly — it is actually emitted unless explicitly excluded).

**Suggestion:**  
Skip `decodeField` for `i == def.timestamp_idx` in the general loop. If
`TimeUS` should not appear as a plotted series, add an explicit `continue` for
that index in the emit loop. If it should appear, emit it from the
already-computed `timestamp * 1e6` to avoid the redundant read.

---

## 5. Reserve Series Vectors Upfront (Low–Medium Impact)

**Current problem:**  
`series.timestamps` and `series.values` grow via `push_back` without a hint,
causing O(log N) reallocations over the life of a long series.

**Suggestion:**  
After the first pass (if two-pass is adopted), count how many data packets
exist for each message type and call `reserve` on the corresponding series
vectors before the decode pass:

```cpp
series.timestamps.reserve(expected_count);
series.values.reserve(expected_count);
```

Even without a two-pass design, a heuristic reserve (e.g., `file_size / avg_packet_size`) on series construction would reduce reallocations significantly.

**Benefit:** Eliminates heap fragmentation and repeated copies for large series.

---

## 6. Zero-Alloc Label Splitting (Low Impact — Cold Path)

**Current problem:**  
`splitLabels` builds a `std::vector<std::string>` with heap-allocated tokens,
called once per FMT message (cold path).

**Suggestion:**  
Use `std::string_view` tokens over the raw buffer instead of copying:

```cpp
static std::vector<std::string_view> splitLabelsView(const char* buf, int len);
```

Fields can then hold `std::string_view` labels during parsing and only
materialize to `std::string` when stored in `ApFieldDef`. Low priority since
FMT parsing is not in the hot path, but it is a clean improvement.

---

---

## `dataload_ardupilot.cpp` Suggestions

### D1. Remove Redundant `series_map` Counting Pass (Trivial)

**Current problem:**  
The write phase iterates `series_map` twice — once to count `total_samples`,
then again to write data:

```cpp
for (const auto& [key, series] : series_map)
    total_samples += series.values.size();   // pass 1 — counting only

for (const auto& [key, series] : series_map)
    plot.pushBack(...);                      // pass 2 — actual write
```

**Suggestion:**  
Add a `getTotalSamples()` accessor to `ArdupilotParser` (the count is trivially
tracked during parsing), or switch to an indeterminate progress bar for the
write phase and eliminate the counting pass entirely.

---

### D2. Throttle `setValue` / `processEvents` Calls (Medium Impact)

**Current problem:**  
`progress_dialog.setValue(...)` and `QApplication::processEvents()` are called
on every series in the write loop, with no change-detection guard. For a log
with hundreds of small series this can mean thousands of redundant UI updates
per second.

The parser's own progress callback already applies a `last_percent` guard —
the write loop does not:

```cpp
// Parser (guarded) ✅
if (pct != last_percent) { last_percent = pct; _progressCb(pos, total); }

// Write loop (unguarded) ❌
progress_dialog.setValue(50 + static_cast<int>(50.0 * written / total_samples));
QApplication::processEvents();
```

**Suggestion:**  
Apply the same guard in the write loop:

```cpp
const int pct = 50 + static_cast<int>(50.0 * written / total_samples);
if (pct != last_percent)
{
    last_percent = pct;
    progress_dialog.setValue(pct);
    QApplication::processEvents();
}
```

**Benefit:** Reduces UI overhead proportionally to the number of series; also
makes cancel checks more responsive during large writes.

---

### D3. Single-Pass Unit `/` Replacement (Trivial)

**Current problem:**  
`std::string::replace(p, 1, "\xe2\x88\x95")` shifts all trailing bytes right
by 2 bytes for each `/` found. For `m/s/s` that is two separate O(n) shifts.

**Suggestion:**  
Build the result in a single forward pass:

```cpp
std::string out;
out.reserve(unit.size() + 4);
for (char ch : unit)
    ch == '/' ? out += "\xe2\x88\x95" : out += ch;
unit = std::move(out);
```

**Benefit:** O(n) instead of O(n·k) where k is the number of slashes. Minor
in practice (unit strings are short), but cleaner.

---

### D4. RAII for `file.unmap` (Low — Safety / Maintainability)

**Current problem:**  
`file.unmap(const_cast<uchar*>(mapped))` appears in two separate branches
(cancel path and normal completion). If an exception is thrown between the
two sites, the mapping leaks.

**Suggestion:**  
Use a scope guard so the unmap always fires on scope exit:

```cpp
auto unmap_guard = qScopeGuard([&]{
    file.unmap(const_cast<uchar*>(mapped));
});
// Remove the two manual unmap calls — guard handles both paths.
```

**Benefit:** Exception-safe, eliminates duplicated cleanup code.

---

### D5. Interleaved `{t, v}` Layout for `ApSeries` (Medium Impact)

**Current problem:**  
`ApSeries` stores timestamps and values in two separate `std::vector<double>`
allocations. The write loop accesses them alternately:

```cpp
for (size_t i = 0; i < series.values.size(); i++)
    plot.pushBack({ series.timestamps[i], series.values[i] });
```

Each iteration touches two non-adjacent cache lines, causing frequent cache
misses for large series.

**Suggestion:**  
Change `ApSeries` to store interleaved `PJ::PlotData::Point` (or equivalent
`{double t, double v}` struct):

```cpp
struct ApSeries {
    std::vector<PJ::PlotData::Point> points;
    std::string unit;
};
```

The write loop then reduces to a single contiguous copy. This change is most
impactful in combination with parser suggestion P5 (pre-reserve vectors) and
P2 (pre-built series keys).

---

### D6. Avoid Temporary Strings for `display_key` (Trivial)

**Current problem:**  
```cpp
const std::string display_key = (show_units && !unit.empty())
    ? key + "(" + unit + ")"
    : key;
```

The ternary creates an extra string copy of `key` in the false branch, and
two temporary strings in the true branch.

**Suggestion:**  
Build in-place using `append`:

```cpp
std::string display_key = key;
if (show_units && !unit.empty())
    display_key.append("(").append(unit).append(")");
```

The false branch now zero-copies (move from `key` if the compiler can elide
it). Low priority since this runs once per series, not per sample.

---

## Combined Impact Estimate

### Parser (`ardupilot_parser.cpp`)

Adopting **P1 + P2 + P3** together would:
- Remove all per-sample string construction and map lookups from the decode loop.
- Make the parser order-independent (no UNIT/MULT/FMTU timing dependency).
- Fix both minor correctness bugs from the units/multipliers audit.

For a typical 10-minute 400 Hz log (~24 M data packets across all message
types), the hot-path changes alone are expected to reduce `parseDataPacket`
CPU time by **40–60%** and peak heap usage by removing temporary string
allocations.

### Plugin (`dataload_ardupilot.cpp`)

**D2** (throttle UI updates) has the most visible effect on UI responsiveness
for logs with many series. **D4** (RAII unmap) is the safest correctness
improvement. **D1 + D3 + D6** are trivial clean-ups that eliminate unnecessary
work. **D5** yields the largest throughput gain in the write phase but requires
changing the `ApSeries` layout across both files.
