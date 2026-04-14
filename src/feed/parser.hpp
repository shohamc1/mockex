#pragma once

#include "common/types.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>
#include <variant>

struct ParsedAdd {
    AddOrderMsg msg;
};

struct ParsedModify {
    ModifyOrderMsg msg;
};

struct ParsedCancel {
    CancelOrderMsg msg;
};

struct ParsedTrade {
    TradeMsg msg;
};

struct ParsedClear {
    ClearMsg msg;
};

struct ParseError {
    const char* reason;
};

using ParsedMsg = std::variant<ParsedAdd, ParsedModify, ParsedCancel,
                               ParsedTrade, ParsedClear, ParseError>;

class Parser {
public:
    static ParsedMsg parse(MsgType type, const uint8_t* data, size_t len);
};
