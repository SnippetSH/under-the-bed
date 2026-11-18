#pragma once

#include <string>

namespace sim {

enum class ErrorCode {
    InvalidArgument,     // 호출자가 넘긴 값이 규칙을 어김 (범위, 크기 등)
    InvalidData,         // 데이터 파일이나 세이브 내용이 형식에 맞지 않음
    UnsupportedVersion,  // 스키마나 세이브 형식 버전이 다름
    Truncated,           // 세이브 바이트가 중간에 끊김
    Io,                  // 파일을 열거나 읽지 못함 (코어 밖 로더에서만 사용)
};

struct Error {
    ErrorCode code;
    std::string message;
};

}  // namespace sim
