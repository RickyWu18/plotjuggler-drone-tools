# ArduPilot Decode Audit: Units & Multipliers

**File audited:** `DataLoadArdupilot/ardupilot_parser.cpp`  
**Date:** 2026-05-25

---

## 1. Decode Pipeline Overview

```
.bin file
  └─ FMT (msg 128)       → _fmtTable[msg_type]      (field layout)
  └─ UNIT packets         → _unitTable[char]          (unit strings)
  └─ MULT packets         → _multTable[char]          (multiplier doubles)
  └─ FMTU packets         → applyFmtu() per field     (assigns unit_id / mult_id)
  └─ Data packets         → decodeField() + scale     (emits ApSeries)
```

FMTU can arrive before its FMT — handled by `_pendingFmtu`, applied when the
FMT is later registered.

---

## 2. Multiplier Table — Seed Values

The constructor pre-seeds `_multTable` before any log packets are parsed.

| ID | Value | ArduPilot official | Match? |
|----|-------|--------------------|--------|
| `'?'` | 1.0    | 1.0 (unknown/identity) | ✅ |
| `'-'` | 1.0    | 1.0 (no scaling)       | ✅ |
| `'0'` | 1.0    | 1.0                    | ✅ |
| `'A'` | 1e-1   | 1e-1                   | ✅ |
| `'B'` | 1e-2   | 1e-2                   | ✅ |
| `'C'` | 1e-3   | 1e-3                   | ✅ |
| `'D'` | 1e-4   | 1e-4                   | ✅ |
| `'E'` | 1e-5   | 1e-5                   | ✅ |
| `'F'` | 1e-6   | 1e-6                   | ✅ |
| `'G'` | 1e-7   | 1e-7                   | ✅ |
| `'I'` | 1e-9   | 1e-9                   | ✅ |
| `'1'` | 1e1    | 1e1                    | ✅ |
| `'2'` | 1e2    | 1e2                    | ✅ |
| `'!'` | 3.6    | 3.6 (m/s → km/h)       | ✅ |
| `'/'` | 3600.0 | 3600.0 (s → h)         | ✅ |

**All 15 entries match ArduPilot's `log_Multipliers[]` table exactly.**
MULT packets can override or extend these at runtime — handled in
`parseMultPacket()`.

---

## 3. Fallback Scaling (No FMTU)

When `field.mult_id == '?'` (FMTU was never applied, i.e. older logs),
the parser applies per-format-char fallbacks:

```cpp
// ardupilot_parser.cpp:354-358
case 'c': case 'C': scaled *= 1e-2; break;   // centidegrees → degrees
case 'e': case 'E': scaled *= 1e-4; break;   // ×1e-4 encoding
case 'L':           scaled *= 1e-7; break;   // lat/lon ×1e-7 degrees
```

| Format | Type | Raw unit | Fallback | Result | Correct? |
|--------|------|----------|----------|--------|----------|
| `c`    | int16  | centidegrees | ×1e-2 | degrees | ✅ |
| `C`    | uint16 | centidegrees | ×1e-2 | degrees | ✅ |
| `e`    | int32  | ×1e-4 encoded| ×1e-4 | SI unit | ✅ |
| `E`    | uint32 | ×1e-4 encoded| ×1e-4 | SI unit | ✅ |
| `L`    | int32  | lat/lon 1e-7 | ×1e-7 | degrees | ✅ |
| all others | — | raw value | no scale | raw | ✅ |

These fallbacks match ArduPilot's documented conventions for legacy format chars.

**No double-scaling risk.** The fallback block is inside the `else` branch of
`if (field.mult_id != '?')`, so it is never executed when FMTU is present.

---

## 4. FMTU Application — `applyFmtu()`

```cpp
// ardupilot_parser.cpp:478-479
if (units16[i] != '\0') def.fields[i].unit_id = units16[i];
if (mults16[i] != '\0') def.fields[i].mult_id = mults16[i];
```

The `!= '\0'` guard skips null bytes, which are padding in fixed-length
ArduPilot strings. This is correct — the valid ID characters (`'0'`=0x30,
`'-'`=0x2D, `'A'`–`'Z'`, etc.) are all non-zero.

Fields beyond the format string length retain their `'?'` defaults, which
triggers the fmt-char fallback. Correct.

---

## 5. Multiplier Application — `parseDataPacket()`

```cpp
// ardupilot_parser.cpp:343-358
if (field.mult_id != '?')
{
  auto mult_it = _multTable.find(field.mult_id);
  if (mult_it != _multTable.end())
    scaled *= mult_it->second;
  // else: mult_id set but unknown — value left unscaled, no warning
}
else { /* fmt-char fallback */ }
```

### Issue 1 — Silent miss on unknown mult_id ✅ Fixed

If FMTU assigns a `mult_id` that is neither in the seed table nor populated by
a MULT packet (e.g., a typo or firmware bug), the lookup fails silently. The
`else`-branch fallback is also skipped because `mult_id != '?'`. The value is
emitted unscaled without any diagnostic message.

**Impact:** Rare (requires a malformed log), but the scaled plot would show raw
integer values instead of physical units with no indication of the error.

**Fix applied:** `qWarning` is now emitted when `mult_it == _multTable.end()`.

---

## 6. Unit Assignment

```cpp
// ardupilot_parser.cpp:362-367
if (series.unit.empty() && field.unit_id != '?')
{
  auto unit_it = _unitTable.find(field.unit_id);
  if (unit_it != _unitTable.end() && !unit_it->second.empty())
    series.unit = unit_it->second;
}
```

Unit is assigned only on the **first data packet** where the unit is resolvable.

### Issue 2 — Unit resolved on first sample only ✅ Fixed

If the first data packet for a series arrives before the UNIT packets have
populated `_unitTable`, `series.unit` stays empty for the lifetime of that
series. Subsequent samples do not re-attempt resolution.

**Impact:** Series names will lack the unit suffix even when "Append units"
is enabled. In practice ArduPilot logs write UNIT packets at the very start of
the file, so this is a rare edge case with truncated or rearranged logs.

**Fix applied:** `ApSeries` now carries `unit_id`. During `parseDataPacket` the
`unit_id` is stored on first touch and resolution is re-attempted on each sample
until it succeeds. A post-pass at the end of `parse()` resolves any remaining
series whose UNIT packet arrived after all data packets.

---

## 7. UNIT / MULT Packet Id Decoding

Both `parseUnitPacket` and `parseMultPacket` decode the `Id` field as `int8_t`
regardless of whether the format char is `'b'` (signed) or `'B'` (unsigned):

```cpp
// ardupilot_parser.cpp:396, 420
int8_t v; memcpy(&v, payload + offset, 1);
type_id = static_cast<char>(v);
```

### Issue 3 — Sign mismatch for 'B' format (cosmetic) ℹ️

All standard ArduPilot multiplier/unit IDs are ASCII characters with values
in `[0x21, 0x7E]` (printable, non-space), so the sign bit is never set and the
cast produces the same result either way. No practical impact.

---

## 8. Float16 Decode — `float16ToDouble()`

Used for format char `'g'` (IEEE 754 half-precision).

```cpp
// ardupilot_parser.cpp:216-219 (denormal branch)
uint32_t e = 1;
uint32_t m = mantissa;
while (!(m & 0x400)) { m <<= 1; e--; }
```

### Issue 4 — Unsigned underflow in denormal normalization ✅ Fixed

`e` was `uint32_t`. If more than 1 shift is needed (mantissa has leading zeros),
`e` wraps from 1 → 0 → 4294967295, producing a wildly wrong float32 exponent.

Example: mantissa = `0b0000000001` (minimum denormal, 9 shifts needed):
- Correct biased float32 exponent: `1 + 127 - 15 - 9 = 104`
- Previously computed: `(1 - 9) as uint32 + 127 - 15` → wrap → garbage

**Impact:** Float16 denormals represent values < 6.1e-5, which are never
produced by ArduPilot sensors. No known practical impact.

**Fix applied:** Changed `e` to `int32_t` and added an underflow guard that
clamps to ±0 when the biased exponent would go below 1:

```cpp
int32_t  e = 1;
uint32_t m = mantissa;
while (!(m & 0x400)) { m <<= 1; e--; }
m &= ~0x400u;
if (e < 1)
  f = (sign << 31);  // underflow to ±0
else
  f = (sign << 31) | (static_cast<uint32_t>(e + 127 - 15) << 23) | (m << 13);
```

---

## 9. Timestamp Decoding

```cpp
// ardupilot_parser.cpp:320-323
uint64_t ts_raw;
memcpy(&ts_raw, payload + ts_offset, 8);
timestamp = static_cast<double>(ts_raw) * 1e-6;
```

Reads raw microseconds as `uint64_t` before converting to seconds. This avoids
accumulation of floating-point rounding that would occur if arithmetic were done
directly on a `double` timestamp. The `1e-6` scaling correctly converts
ArduPilot's `TimeUS` (microseconds) to seconds.

For the longest conceivable flight (< 834 days), the `double` mantissa (53 bits)
represents the integer microsecond value exactly. ✅

---

## 10. Unit Display — ASCII `/` Substitution

```cpp
// dataload_ardupilot.cpp:119-122
for (size_t p = 0; (p = unit.find('/', p)) != std::string::npos; )
{
  unit.replace(p, 1, "\xe2\x88\x95");   // U+2215 DIVISION SLASH
  p += 3;
}
```

Replaces ASCII `/` (U+002F) in unit strings (e.g., `m/s`, `rad/s`) with the
Unicode division slash (U+2215, UTF-8 `E2 88 95`) before forming the
`display_key`. This prevents PlotJuggler from interpreting the slash as a
series-name path separator. ✅

---

## Summary Table

| # | Location | Severity | Finding |
|---|----------|----------|---------|
| 1 | `parseDataPacket` L343–358 | ✅ Fixed | `qWarning` now emitted when `mult_id` is set but not in `_multTable` |
| 2 | `parseDataPacket` L362–367 | ✅ Fixed | `ApSeries` stores `unit_id`; post-pass resolves units after all UNIT packets are seen |
| 3 | `parseUnitPacket` / `parseMultPacket` | ℹ️ Cosmetic | `int8_t` used to read Id even when fmt_char is `'B'`; no practical impact |
| 4 | `float16ToDouble` L216–219 | ✅ Fixed | Changed `e` to `int32_t`; underflow guard clamps denormal to ±0 |
| — | Multiplier seed table | ✅ Correct | All 15 entries match ArduPilot's `log_Multipliers[]` exactly |
| — | Fmt-char fallbacks (c/C/e/E/L) | ✅ Correct | Match ArduPilot conventions; no double-scaling risk |
| — | FMTU null-byte guard | ✅ Correct | `!= '\0'` correctly skips padding; valid IDs are never 0x00 |
| — | Timestamp decode | ✅ Correct | uint64 → double×1e-6 preserves precision for all practical log durations |
| — | Unit `/` → U+2215 | ✅ Correct | Prevents path-separator collision in PlotJuggler series tree |
