// 依存ライブラリなしの最小 JSON ライタ。
// 小数はロケールに依存しないよう自前で固定小数点に整形する
// (printf("%f") はロケールにより小数点が "," になる場合があるため)。
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace mb::detail {

class JsonWriter {
 public:
  explicit JsonWriter(bool pretty) : pretty_(pretty) {}

  void begin_object() { open('{'); }
  void end_object() { close('}'); }
  void begin_array() { open('['); }
  // 数値などスカラーだけの配列を 1 行で出す (pretty 時のサイズ削減)
  void begin_inline_array() { open('[', /*inline_items=*/true); }
  void end_array() { close(']'); }

  void key(const std::string& k) {
    comma_and_indent();
    write_string(k);
    out_ += pretty_ ? ": " : ":";
    after_key_ = true;
  }

  void value(const std::string& v) { before_value(); write_string(v); }
  void value(const char* v) { value(std::string(v)); }
  void value(bool v) { before_value(); out_ += v ? "true" : "false"; }
  void value(int v) { value(static_cast<std::int64_t>(v)); }
  void value(std::int64_t v) { before_value(); out_ += std::to_string(v); }
  void value(std::uint64_t v) { before_value(); out_ += std::to_string(v); }
  // 小数点以下 decimals 桁の固定小数点 (decimals <= 6)
  void value_fixed(double v, int decimals) { before_value(); out_ += format_fixed(v, decimals); }

  const std::string& str() const { return out_; }

  static std::string format_fixed(double v, int decimals) {
    if (!std::isfinite(v)) return "0";
    std::int64_t scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    const bool negative = v < 0;
    const auto scaled = static_cast<std::int64_t>(std::llround(std::fabs(v) * static_cast<double>(scale)));
    std::string s = negative && scaled != 0 ? "-" : "";
    s += std::to_string(scaled / scale);
    if (decimals > 0) {
      std::string frac = std::to_string(scaled % scale);
      s += '.';
      s.append(static_cast<size_t>(decimals) - frac.size(), '0');
      s += frac;
    }
    return s;
  }

 private:
  struct Level {
    bool empty = true;          // まだ要素がない
    bool inline_items = false;  // 要素を改行せずに並べる
  };

  void open(char c, bool inline_items = false) {
    before_value();
    out_ += c;
    stack_.push_back(Level{true, inline_items || (!stack_.empty() && stack_.back().inline_items)});
  }

  void close(char c) {
    const Level level = stack_.back();
    stack_.pop_back();
    if (!level.empty && !level.inline_items) newline_indent();
    out_ += c;
  }

  void before_value() {
    if (after_key_) {
      after_key_ = false;
      return;
    }
    if (!stack_.empty()) comma_and_indent();
  }

  void comma_and_indent() {
    if (stack_.empty()) return;
    Level& level = stack_.back();
    if (!level.empty) out_ += ',';
    const bool first = level.empty;
    level.empty = false;
    if (level.inline_items) {
      if (pretty_ && !first) out_ += ' ';
      return;
    }
    newline_indent();
  }

  void newline_indent() {
    if (!pretty_) return;
    out_ += '\n';
    out_.append(stack_.size() * 2, ' ');
  }

  void write_string(const std::string& s) {
    out_ += '"';
    for (unsigned char c : s) {
      switch (c) {
        case '"': out_ += "\\\""; break;
        case '\\': out_ += "\\\\"; break;
        case '\n': out_ += "\\n"; break;
        case '\r': out_ += "\\r"; break;
        case '\t': out_ += "\\t"; break;
        default:
          if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out_ += buf;
          } else {
            out_ += static_cast<char>(c);
          }
      }
    }
    out_ += '"';
  }

  bool pretty_;
  bool after_key_ = false;
  std::vector<Level> stack_;
  std::string out_;
};

}  // namespace mb::detail
