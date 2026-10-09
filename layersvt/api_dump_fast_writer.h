/* Copyright (c) 2015-2026 The Khronos Group Inc.
 * Copyright (c) 2015-2026 Valve Corporation
 * Copyright (c) 2015-2026 LunarG, Inc.
 * Copyright (C) 2015-2016 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <streambuf>
#include <string_view>
#include <type_traits>

// Writes the pieces of one dump record to a std::ostream's streambuf in as few calls as possible.
//
// The JSON dump is made of a great many tiny pieces: an indentation, a quote, a key, a quote and a
// colon, then a value, and so on - around thirty for one leaf field. Inserting each into the ostream
// costs a sentry construction and destruction, the padding logic of the formatted output functions
// (which runs for every string even when no width is set), and one virtual call into the streambuf.
// Profiled on a device, that machinery was over half of the render thread's CPU while dumping.
//
// This class has the same insertion syntax - `writer << a << b << c` - but gathers the pieces in a
// small buffer on the stack and hands them to the streambuf in one call when the buffer fills or the
// writer goes out of scope. The text produced is the text ostream would have produced for the same
// values: integers in decimal, floating point as printf's %g at the stream's precision, characters and
// strings as they are, and so on. Anything this class does not know how to format is passed to the
// ostream itself, after what has been gathered so far, so the order of the output is always preserved
// and a value of any streamable type can be inserted.
//
// Differences from the ostream, none of which matter to the dump: it does not honour a field width
// (nothing sets one while dumping JSON), it does not skip output on a stream that has gone bad (the
// streambuf is written regardless, and a short write marks the stream bad), and a null C string writes
// nothing rather than setting the stream's badbit.
//
// A writer must not outlive the stream it was made for, and while one is alive nothing else may be
// written to that stream except through it - its pieces would come out of order. Not thread safe, like
// the stream: api_dump only formats under its output mutex.
class ApiDumpFastWriter {
   public:
    // Large enough for a whole JSON field, which is the unit callers gather. A longer piece is written
    // straight through, so this is not a limit on what can be inserted.
    static constexpr size_t kCapacity = 512;

    explicit ApiDumpFastWriter(std::ostream &stream)
        : stream_(stream), sink_(stream.rdbuf()), precision_(static_cast<int>(stream.precision())) {}
    ~ApiDumpFastWriter() { flush(); }

    ApiDumpFastWriter(const ApiDumpFastWriter &) = delete;
    ApiDumpFastWriter &operator=(const ApiDumpFastWriter &) = delete;

    // The number of significant digits floating point values are written with. Starts as the stream's
    // own precision, which is what the ostream would have used.
    void setPrecision(int precision) { precision_ = precision; }

    // Hands everything gathered so far to the streambuf.
    void flush() {
        if (length_ != 0) {
            write(buffer_, length_);
            length_ = 0;
        }
    }

    // Writes the digits of an integer in hexadecimal, lower case and without a prefix: what the ostream
    // writes for the integer under std::hex.
    template <typename T>
    ApiDumpFastWriter &hex(T value) {
        static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>, "hex takes an integer");
        char digits[2 * sizeof(T) + 1];
        const auto result = std::to_chars(digits, digits + sizeof(digits), static_cast<std::make_unsigned_t<T>>(value), 16);
        append(digits, static_cast<size_t>(result.ptr - digits));
        return *this;
    }

    template <typename T>
    ApiDumpFastWriter &operator<<(const T &value) {
        using Decayed = std::decay_t<T>;
        if constexpr (std::is_convertible_v<const T &, std::string_view>) {
            // A string, a string_view, or anything that is one: char arrays and char pointers included.
            if constexpr (std::is_pointer_v<Decayed>) {
                if (value != nullptr) appendView(std::string_view(value));
            } else {
                appendView(std::string_view(value));
            }
        } else if constexpr (std::is_same_v<Decayed, bool>) {
            // Without boolalpha, which nothing sets here.
            appendChar(value ? '1' : '0');
        } else if constexpr (isCharType<Decayed>) {
            // The ostream writes char, signed char and unsigned char as the character, not the number.
            appendChar(static_cast<char>(value));
        } else if constexpr (std::is_integral_v<Decayed>) {
            char digits[24];
            const auto result = std::to_chars(digits, digits + sizeof(digits), value);
            append(digits, static_cast<size_t>(result.ptr - digits));
        } else if constexpr (std::is_floating_point_v<Decayed>) {
            appendFloat(value);
        } else if constexpr (std::is_pointer_v<Decayed> && !std::is_function_v<std::remove_pointer_t<Decayed>>) {
            appendPointer(static_cast<const void *>(value));
        } else {
            // Not something this class formats: the ostream does, after what has gone before.
            flush();
            stream_ << value;
        }
        return *this;
    }

   private:
    template <typename T>
    static constexpr bool isCharType = std::is_same_v<T, char> || std::is_same_v<T, signed char> || std::is_same_v<T, unsigned char>;

    void write(const char *data, size_t length) {
        if (sink_ == nullptr) {
            return;
        }
        if (static_cast<size_t>(sink_->sputn(data, static_cast<std::streamsize>(length))) != length) {
            stream_.setstate(std::ios_base::badbit);
        }
    }

    void append(const char *data, size_t length) {
        if (length > kCapacity - length_) {
            flush();
            if (length >= kCapacity) {
                write(data, length);
                return;
            }
        }
        std::memcpy(buffer_ + length_, data, length);
        length_ += length;
    }

    void appendView(std::string_view view) { append(view.data(), view.size()); }

    void appendChar(char c) {
        if (length_ == kCapacity) {
            flush();
        }
        buffer_[length_++] = c;
    }

    // printf's %g at the current precision, which is what the ostream formats a floating point value
    // with when no floatfield is set. Formatted with snprintf rather than std::to_chars because the
    // C++ library on every platform this runs on has the former, and it writes inf and nan the same
    // way the ostream does.
    template <typename T>
    void appendFloat(T value) {
        // Room for a sign, 17 significant digits, a point and an exponent - and then some. A precision
        // beyond what fits goes to the ostream, as does anything snprintf could not fit.
        char text[64];
        int length = -1;
        if (precision_ >= 0 && precision_ <= 40) {
            if constexpr (std::is_same_v<T, long double>) {
                length = std::snprintf(text, sizeof(text), "%.*Lg", precision_, value);
            } else {
                length = std::snprintf(text, sizeof(text), "%.*g", precision_, static_cast<double>(value));
            }
        }
        if (length < 0 || static_cast<size_t>(length) >= sizeof(text)) {
            flush();
            const std::streamsize previous = stream_.precision(precision_);
            stream_ << value;
            stream_.precision(previous);
            return;
        }
        append(text, static_cast<size_t>(length));
    }

    // What the ostream writes for a pointer is whatever printf's %p writes, which differs between
    // platforms (the null pointer, zero padding, the case of the digits). Android's is "0x" and the
    // digits in lower case, which is written directly; everywhere else the ostream is left to do it.
    void appendPointer(const void *pointer) {
#if defined(__ANDROID__)
        char text[2 + 2 * sizeof(uintptr_t)] = {'0', 'x'};
        const auto result = std::to_chars(text + 2, text + sizeof(text), reinterpret_cast<uintptr_t>(pointer), 16);
        append(text, static_cast<size_t>(result.ptr - text));
#else
        flush();
        stream_ << pointer;
#endif
    }

    std::ostream &stream_;
    std::streambuf *sink_;
    int precision_;
    size_t length_ = 0;
    char buffer_[kCapacity];
};
