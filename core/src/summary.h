// 要約テキストの内部 API (ユニットテスト用に公開)。公開 API は mobilebench/result.h の
// make_summary_parts / make_summary_text。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "mobilebench/result.h"

namespace mb::detail {

// 途中で切らない行のまとまり (先頭の情報、テスト 1 本、診断の 1 項目)
using SummaryBlock = std::vector<std::string>;

// パートの見出しと終端の行を除いた、要約の本文 (改行を含まない行のまとまり)
std::vector<SummaryBlock> summary_blocks(const SuiteResult& result);

// パートの見出し (part i/n を除く部分)。例: "MobileBench v0.2.1 | app 0.2.1 build 13 (abc1234) | run 2026-..."
std::string summary_run_header(const SuiteResult& result);

// 文字列の文字数 (Unicode のコードポイント数。UTF-8 として数える)
std::size_t utf8_char_count(const std::string& s);

}  // namespace mb::detail
