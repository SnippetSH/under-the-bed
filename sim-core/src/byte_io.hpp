#pragma once

// 세이브 바이트 입출력. 플랫폼과 무관하게 리틀 엔디언으로 고정해요.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sim::detail {

class ByteWriter {
public:
    void u8(std::uint8_t v) { bytes_.push_back(static_cast<std::byte>(v)); }

    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            u8(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }

    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            u8(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }

    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }

    void str(std::string_view s) {
        u64(s.size());
        for (const char c : s) {
            u8(static_cast<std::uint8_t>(c));
        }
    }

    [[nodiscard]] std::span<const std::byte> bytes() const { return bytes_; }
    [[nodiscard]] std::vector<std::byte> take() { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

// 범위를 넘으면 0을 돌려주고 실패 플래그를 세워요. 호출자는 ok()로 한 번에 확인해요.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    std::uint8_t u8() {
        if (pos_ >= bytes_.size()) {
            failed_ = true;
            return 0;
        }
        return std::to_integer<std::uint8_t>(bytes_[pos_++]);
    }

    std::uint32_t u32() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(u8()) << (8 * i);
        }
        return v;
    }

    std::uint64_t u64() {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(u8()) << (8 * i);
        }
        return v;
    }

    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    double f64() { return std::bit_cast<double>(u64()); }

    std::string str() {
        const std::uint64_t size = u64();
        if (failed_ || size > remaining()) {
            failed_ = true;
            return {};
        }
        std::string s;
        s.reserve(static_cast<std::size_t>(size));
        for (std::uint64_t i = 0; i < size; ++i) {
            s.push_back(static_cast<char>(u8()));
        }
        return s;
    }

    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - pos_; }
    [[nodiscard]] bool ok() const { return !failed_; }
    [[nodiscard]] bool at_end() const { return pos_ == bytes_.size(); }
    void fail() { failed_ = true; }

private:
    std::span<const std::byte> bytes_;
    std::size_t pos_ = 0;
    bool failed_ = false;
};

}  // namespace sim::detail
