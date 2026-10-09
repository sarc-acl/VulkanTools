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
 *
 * Author: Lenny Komow <lenny@lunarg.com>
 * Author: Shannon McPherson <shannon@lunarg.com>
 * Author: David Pinedo <david@lunarg.com>
 * Author: Charles Giessen <charles@lunarg.com>
 */

#pragma once

#define VK_NO_PROTOTYPES

#include "vulkan/vk_layer.h"
#include "vk_layer_table.h"
#include "VK_ANDROID_frame_boundary.h"
#include "api_dump_async_buf.h"
#include "api_dump_fast_writer.h"
#include <vulkan/utility/vk_dispatch_table.h>

#include <vulkan/layer/vk_layer_settings.hpp>

// Include the video headers so we can print types that come from them
#include "vk_video/vulkan_video_codecs_common.h"
#include "vk_video/vulkan_video_codec_h264std.h"
#include "vk_video/vulkan_video_codec_h264std_decode.h"
#include "vk_video/vulkan_video_codec_h264std_encode.h"
#include "vk_video/vulkan_video_codec_h265std.h"
#include "vk_video/vulkan_video_codec_h265std_decode.h"
#include "vk_video/vulkan_video_codec_h265std_encode.h"
#include "vk_video/vulkan_video_codec_av1std.h"
#include "vk_video/vulkan_video_codec_av1std_decode.h"

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <fstream>
#include <mutex>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <map>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <utility>

#if defined(_WIN32) && !defined(NDEBUG)
#include <crtdbg.h>
#endif

#if defined(_WIN32)
// Used by GetCurrentProcessName. NOMINMAX avoids windows.h's min/max macros shadowing std::min/max,
// used throughout this header.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef ANDROID
#include <memory>
#include <string_view>

#include <android/log.h>
#include <sys/system_properties.h>
// Disable warning about bitshift precedence
#pragma GCC diagnostic ignored "-Wshift-op-parentheses"

#endif  // ANDROID

#if defined(__GNUC__) && __GNUC__ >= 4
#define EXPORT_FUNCTION __attribute__((visibility("default")))
#elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x590)
#define EXPORT_FUNCTION __attribute__((visibility("default")))
#else
#define EXPORT_FUNCTION
#endif

#if defined(WIN32)
// Disable warning about bitshift precedence
#pragma warning(disable : 4554)
#endif

extern "C" {
// Forward declarations for dispatch
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_vkGetInstanceProcAddr(VkInstance instance, const char* pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL layer_vkGetDeviceProcAddr(VkDevice device, const char* pName);
}

#define MAX_STRING_LENGTH 1024

// Defines for utilized environment variables.
#define kSettingsKeyFile "file"
#define kSettingsKeyLogFilename "log_filename"
#define kSettingsKeyOutputFormat "output_format"
#define kSettingsKeyDetailedOutput "detailed"
#define kSettingsKeyNoAddr "no_addr"
#define kSettingsKeyFlush "flush"
#define kSettingsKeyPreDump "pre_dump"
#define kSettingsKeyOutputRange "output_range"
#define kSettingsKeyOutputRangeQueueSubmits "output_range_queue_submits"
#define kSettingsKeyTimestamp "timestamp"
#define kSettingsKeyIndentSize "indent_size"
#define kSettingsKeyShowTypes "show_types"
#define kSettingsKeyNameSize "name_size"
#define kSettingsKeyTypeSize "type_size"
#define kSettingsKeyUseSpaces "use_spaces"
#define kSettingsKeyShowShader "show_shader"
#define kSettingsKeyShowThreadAndFrame "show_thread_and_frame"
#define kSettingsKeyCaptureTrigger "capture_trigger"
#define kSettingsKeyCaptureTriggerBoundary "capture_trigger_boundary"
#define kSettingsKeyAlwaysDumpSetup "always_dump_setup"
#define kSettingsKeyShowEnumValue "show_enum_value"
#define kSettingsKeyFloatPrecision "float_precision"
#define kSettingsKeyShowCommandNumbers "show_command_numbers"
#define kSettingsKeyCaptureProcessName "capture_process_name"
#define kSettingsKeyAsyncWrite "async_write"
#define kSettingsKeyBufferSizeKb "buffer_size_kb"

// Default for kSettingsKeyBufferSizeKb when async_write is on and no size is given.
#define kDefaultAsyncBufferSizeKb 2048

// The Android property backing kSettingsKeyCaptureTrigger. The layer settings library only reads a
// property once at instance creation, so the trigger has to be polled directly to be able to change
// while the app runs.
// Note the layer settings library would also accept the compatibility spelling
// debug.apidump.capture_trigger, which is not polled here: a trigger set under that name would
// enable triggered mode but then never appear to change.
#define kCaptureTriggerProperty "debug.vulkan.lunarg_api_dump.capture_trigger"

// We want to dump all extensions even beta extensions.
#ifndef VK_ENABLE_BETA_EXTENSIONS
#error "VK_ENABLE_BETA_EXTENSIONS not defined!"
#endif

// Ensure we are properly setting VK_USE_PLATFORM_METAL_EXT, VK_USE_PLATFORM_IOS_MVK, and VK_USE_PLATFORM_MACOS_MVK.
#if __APPLE__

// TODO: Add Metal support
// #ifndef VK_USE_PLATFORM_METAL_EXT
// #error "VK_USE_PLATFORM_METAL_EXT not defined!"
// #endif

#include <TargetConditionals.h>

#if TARGET_OS_IOS

#ifndef VK_USE_PLATFORM_IOS_MVK
#error "VK_USE_PLATFORM_IOS_MVK not defined!"
#endif

#endif  //  TARGET_OS_IOS

#if TARGET_OS_OSX

#ifndef VK_USE_PLATFORM_MACOS_MVK
#error "VK_USE_PLATFORM_MACOS_MVK not defined!"
#endif

#endif  // TARGET_OS_OSX

#endif  // __APPLE__

#if defined(VK_USE_64_BIT_PTR_DEFINES) && VK_USE_64_BIT_PTR_DEFINES == 1
#define TYPE_ERASE_HANDLE(handle) static_cast<void *>(handle)
#else
#define TYPE_ERASE_HANDLE(handle) static_cast<uint64_t>(handle)
#endif

enum class ApiDumpFormat {
    Text,
    Html,
    Json,
};

// What advances and checks the frame counter used by output_range/output_range_queue_submits and
// capture_trigger: vkQueuePresentKHR for Frames (the default, matching prior behavior), or
// vkQueueSubmit/vkQueueSubmit2/vkQueueSubmit2KHR for QueueSubmits, needed for compute-only
// workloads that never present.
enum class ApiDumpCaptureBoundary {
    Frames,
    QueueSubmits,
};

// Walks a pNext chain looking for a VkFrameBoundaryEXT (VK_EXT_frame_boundary) marking the end of a
// frame, so vkQueueSubmit/vkQueueSubmit2 can advance the real frame counter for compute/off-screen
// workloads that submit through a frame boundary chained onto the submission instead of presenting.
static bool IsFrameBoundaryEnd(const void *pNext) {
    for (const VkBaseInStructure *current = static_cast<const VkBaseInStructure *>(pNext); current != nullptr;
         current = current->pNext) {
        if (current->sType == VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT) {
            return reinterpret_cast<const VkFrameBoundaryEXT *>(current)->flags & VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
        }
    }
    return false;
}

static const uint64_t OUTPUT_RANGE_UNLIMITED = 0;
static const uint64_t OUTPUT_RANGE_INTERVAL_DEFAULT = 1;

struct FrameRange {
    uint64_t start_frame;  // The range begins on this frame, inclusive.
    uint64_t frame_count;  // If value is OUTPUT_RANGE_UNLIMITED, dump continues without limit.
    uint64_t interval;     // Rate at which frames are dumped. A value of 3 will dump every third frame.
};

class ConditionalFrameOutput {
    bool use_conditional_output = false;
    std::set<uint64_t> frames;
    std::vector<FrameRange> ranges;

    struct NumberToken {
        uint64_t value;
        uint32_t length;
    };

    NumberToken parseNumber(std::string str, uint32_t current_char) {
        uint32_t length = 0;
        while (current_char + length < str.size() && str[current_char + length] >= '0' && str[current_char + length] <= '9') {
            length++;
        }
        if (length > 0) {
            uint64_t value = std::atol(&str[current_char]);
            return NumberToken{value, length};
        } else {
            return NumberToken{0, 0};
        }
    }

    void printErrorMsg(const char *msg) {
#ifdef ANDROID
        __android_log_print(ANDROID_LOG_DEBUG, "api_dump", "%s", msg);
#else
        fprintf(stderr, "%s", msg);
#endif
    }

   public:
    /* Parses a string for a comma seperated list of frames & frame ranges
     * where frames are singular integers and frame ranges are of the following
     * format: "S-C-I" with S is the start frame, C is the count of frames to dump,
     * and I the interval between dumped frames.
     * Valid range strings: "2,3,5", "4-4-2", "3-6, 10-2"
     */
    bool parseConditionalFrameRange(std::string range_str) {
        uint32_t current_char = 0;

        if (range_str.empty()) {
            printErrorMsg("Conditional range error: format string was empty\n");
            return false;
        }

        while (current_char < range_str.size()) {
            NumberToken frame_number = parseNumber(range_str, current_char);
            if (frame_number.length <= 0) {
                printErrorMsg("Conditional range error: Invalid frame number\n");
                return false;
            }
            current_char += frame_number.length;

            // Range of frames
            if (range_str[current_char] == '-') {
                current_char++;
                if (current_char >= range_str.size()) {
                    printErrorMsg("Conditional range error: Must have number for frame count\n");
                    return false;
                }
                NumberToken frame_count = parseNumber(range_str, current_char);
                if (frame_count.length <= 0) {
                    printErrorMsg("Conditional range error: Invalid frame count\n");
                    return false;
                }
                current_char += frame_count.length;

                if (current_char >= range_str.size()) {
                    // Frame Range w/o interval
                    ranges.push_back(FrameRange{frame_number.value, frame_count.value, 1l});
                    use_conditional_output = true;
                    return true;
                }

                else if (range_str[current_char] == '-') {
                    current_char++;
                    if (current_char >= range_str.size()) {
                        printErrorMsg("Conditional range error: Must have number for frame interval \n");
                        return false;
                    }

                    NumberToken frame_interval = parseNumber(range_str, current_char);
                    if (frame_interval.length <= 0) {
                        printErrorMsg("Conditional range error: Invalid interval\n");
                        return false;
                    }

                    // Frame Range w/ interval
                    ranges.push_back(FrameRange{frame_number.value, frame_count.value, frame_interval.value});

                    current_char += frame_interval.length;
                    if (current_char >= range_str.size()) {
                        use_conditional_output = true;
                        return true;
                    }
                } else {
                    // Frame Range w/o interval
                    ranges.push_back(FrameRange{frame_number.value, frame_count.value, 1l});
                }
            } else {
                // Single frame capture
                frames.insert(frame_number.value);
            }
            if (range_str[current_char] == ',') {
                current_char++;
            }
        }
        use_conditional_output = true;
        return true;
    }

    // Return true if either use_conditional_output is false or if frame_count is within
    // the provided frame ranges
    bool isFrameInRange(uint64_t frame_number) const {
        if (!use_conditional_output) return true;
        for (auto &range : ranges) {
            if (range.start_frame <= frame_number) {
                if (range.frame_count == OUTPUT_RANGE_UNLIMITED) {
                    if (range.interval == OUTPUT_RANGE_INTERVAL_DEFAULT) {
                        return true;
                    } else {
                        return (frame_number - range.start_frame) % range.interval == 0;
                    }

                } else if (range.start_frame + range.frame_count > frame_number) {
                    if (range.interval == OUTPUT_RANGE_INTERVAL_DEFAULT) {
                        return true;
                    } else {
                        return (frame_number - range.start_frame) % range.interval == 0;
                    }
                }
            }
        }
        if (frames.count(frame_number) > 0) {
            return true;
        }
        return false;
    }
};

#ifdef __ANDROID__
template <class char_type = char, class traits = std::char_traits<char_type>>
class AndroidLogcatBuf final : public std::basic_streambuf<char_type, traits> {
   public:
    class LogWriter {
       public:
        virtual void write(const std::basic_string<char_type, traits> &data) = 0;
        virtual ~LogWriter() {}
    };

    // bufsize should be smaller than 0x400, because `__android_log_print` uses `vsnprintf` with a 1024 buffer size. If bufsize here
    // is greater or close to 1024, the log will be truncated.
    AndroidLogcatBuf(std::unique_ptr<LogWriter> log_writer, size_t bufsize = 0x200)
        : buffer_(std::make_unique<char_type[]>(bufsize)), bufsize_(bufsize), log_writer_(std::move(log_writer)) {
        // We use the last position of buffer_ to store the overflow character. We use 0 as a sentinel element to indicate if this
        // overflow slot is in use.
        this->setp(buffer_.get(), buffer_.get() + bufsize_ - 1);
    }
    ~AndroidLogcatBuf() = default;

   private:
    using int_type = typename traits::int_type;
    int_type overflow(int_type c) override {
        // Conform with the C++ standard. Return not eof when called with eof:
        // https://en.cppreference.com/w/cpp/io/basic_stringbuf/overflow.
        if (c == traits::eof()) {
            return traits::not_eof(c);
        }
        buffer_[bufsize_ - 1] = traits::to_char_type(c);
        flushPending();
        buffer_[bufsize_ - 1] = traits::to_char_type(0);
        return traits::not_eof(c);
    }

    int sync() override {
        flushPending();
        if (!pending_content_.empty()) {
            // Also write data after the last new line.
            log_writer_->write(pending_content_);
            pending_content_.clear();
        }
        return 0;
    }

    // Flush pending_content_ + buffer_ up to the last new line to the log writer, and always move whatever is left in buffer_ to
    // pending_content_.
    void flushPending() {
        auto len = this->pptr() - this->pbase();
        if (this->pptr() == buffer_.get() + bufsize_ - 1 && buffer_[bufsize_ - 1] != traits::to_char_type(0)) {
            len++;
        }
        constexpr size_t npos = std::basic_string_view<char_type, traits>::npos;
        std::basic_string_view<char_type, traits> buf_content(this->pbase(), len);
        std::size_t last_new_line_pos = buf_content.find_last_of(traits::to_char_type('\n'));
        if (last_new_line_pos == npos) {
            pending_content_.append(buf_content);
            this->setp(buffer_.get(), buffer_.get() + bufsize_ - 1);
            return;
        }
        std::basic_string_view<char_type, traits> before_new_line = buf_content.substr(0, last_new_line_pos);
        std::basic_string<char_type, traits> to_print = std::move(pending_content_);
        to_print.append(before_new_line);
        if (!to_print.empty()) {
            log_writer_->write(to_print);
        }
        std::basic_string_view<char_type, traits> after_new_line = buf_content.substr(last_new_line_pos + 1);
        pending_content_ = std::basic_string<char_type, traits>(after_new_line);
        this->setp(buffer_.get(), buffer_.get() + bufsize_ - 1);
    }

    std::unique_ptr<char_type[]> buffer_;
    size_t bufsize_;
    std::basic_string<char_type, traits> pending_content_;
    std::unique_ptr<LogWriter> log_writer_;
};

class AndroidLogcatWriter final : public AndroidLogcatBuf<>::LogWriter {
   public:
    AndroidLogcatWriter() = default;
    void write(const std::string &content) override { __android_log_print(ANDROID_LOG_INFO, "api_dump", "%s", content.c_str()); }
};
#endif

static const char *GetDefaultPrefix() {
#ifdef __ANDROID__
    return "apidump";
#else
    return "APIDUMP";
#endif
}

// The name of the process the layer is loaded into, used to answer kSettingsKeyCaptureProcessName.
// Any directory prefix and anything from the first space onward is stripped, so this is a bare
// executable/package name comparable against what a caller derives from a command line.
static std::string GetCurrentProcessName() {
    std::string application_name;
#if defined(__APPLE__) || defined(__FreeBSD__)
    application_name = getprogname();
#elif defined(__linux__)
    // Covers Android too: it is a Linux kernel, and there is no more specific API that reports
    // this without a JNI round trip through ActivityThread.
    char command_line[1024] = {};
    FILE *fp = fopen("/proc/self/cmdline", "r");
    if (fp != nullptr) {
        char *str = fgets(command_line, sizeof(command_line), fp);
        fclose(fp);
        if (str != nullptr) {
            std::string cmd_line_string = command_line;
            std::size_t location = cmd_line_string.find_last_of('/');
            if (location != std::string::npos) {
                cmd_line_string = cmd_line_string.substr(location + 1);
            }
            application_name = cmd_line_string.substr(0, cmd_line_string.find(' '));
        }
    }
#elif defined(_WIN32)
    char module_name[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, module_name, MAX_PATH) > 0) {
        std::string module_name_string = module_name;
        std::size_t location = module_name_string.find_last_of('\\');
        if (location != std::string::npos) {
            module_name_string = module_name_string.substr(location + 1);
        }
        application_name = module_name_string.substr(0, module_name_string.find(' '));
    }
#endif
    return application_name;
}

class ApiDumpSettings {
   public:
    ApiDumpSettings() : output_stream(std::cout.rdbuf()) {
#ifdef __ANDROID__
        android_logcat_buf = std::make_unique<AndroidLogcatBuf<>>(std::make_unique<AndroidLogcatWriter>());
        output_stream.rdbuf(android_logcat_buf.get());
#endif
    }

    ~ApiDumpSettings() {
        // Nothing was opened for a process capture_process_name excludes - see init - so there is
        // nothing to close either.
        if (!process_matches_capture_name) {
          return;
        }

        if (output_format == ApiDumpFormat::Html) {
            // Close off html
            output_stream << "</details></div></body></html>";
        } else if (output_format == ApiDumpFormat::Json) {
            // Close off json
            output_stream << "\n]" << std::endl;
        }

        // Everything above only reached the buffer: wait for the background writer to put it in the
        // file, and stop the writer, before the objects it uses are destroyed.
        if (async_file_buf) {
            async_file_buf->close();
        }
    }

    // Gets what has been written so far on its way to the file, without waiting for the background
    // writer: with async_write it queues it for the writer; otherwise, when flush is off, it flushes
    // the file stream, so a buffer sized by buffer_size_kb never holds more than one boundary's worth.
    // Called at frame and queue submission boundaries so the file is never more than one of them behind
    // the application, which is what bounds what a kill can lose. A no-op when flush is on, since
    // every call has already been flushed.
    // Must be called with the output mutex held, like everything else that writes to the stream.
    void handOffOutput() {
        if (async_file_buf) {
            async_file_buf->handOff();
        } else if (!should_flush && output_file_stream.is_open()) {
            output_stream.flush();
        }
    }

    // Gets everything written so far into the output file, waiting up to `timeout` for the background
    // writer if there is one. Returns false if that did not complete in time or a write failed. Must
    // be called with the output mutex held.
    bool drainOutput(std::chrono::milliseconds timeout) {
        if (async_file_buf) {
            return async_file_buf->drain(timeout);
        }
        if (output_file_stream.is_open()) {
            output_stream.flush();
            return output_stream.good();
        }
        return true;
    }

    void setupInterFrameOutputFormatting(uint64_t frame_count) const /*name change? */
    {
        static bool hasPrintedAFrame = false;
        switch (format()) {
            case (ApiDumpFormat::Html):
                if (frame_count > 0) {
                    if (wasPreviousFrameRecorded(frame_count)) output_stream << "</details>";
                }
                if (isFrameRecorded(frame_count)) {
                    output_stream << "<details class='frm'><summary>Frame ";
                    if (show_thread_and_frame) {
                        output_stream << frame_count;
                    }
                    output_stream << "</summary>";
                }
                break;

            case (ApiDumpFormat::Json):

                if (frame_count > 0) {
                    if (wasPreviousFrameRecorded(frame_count)) output_stream << "\n" << indentation(1) << "]\n}";
                }
                if (isFrameRecorded(frame_count)) {
                    if (!hasPrintedAFrame) {
                        hasPrintedAFrame = true;
                    } else {
                        output_stream << ",\n";
                    }
                    output_stream << "{\n";
                    if (show_thread_and_frame) {
                        output_stream << indentation(1) << "\"frameNumber\" : \"" << frame_count << "\",\n";
                    }
                    if (!isFrameInRange(frame_count)) {
                        // The only way this frame object exists at all while being out of range is
                        // always_dump_setup, so this frame holds setup commands only.
                        output_stream << indentation(1) << "\"isSetupFrame\" : true,\n";
                    }
                    output_stream << indentation(1) << "\"apiCalls\" :\n";
                    output_stream << indentation(1) << "[\n";
                }
                break;
            case (ApiDumpFormat::Text):
                break;
            default:
                break;
        }
    }

    void closeFrameOutput() const {
        switch (format()) {
            case (ApiDumpFormat::Html):
                output_stream << "</details>";
                break;
            case (ApiDumpFormat::Json):
                output_stream << "\n" << indentation(1) << "]\n}";
                break;
            case (ApiDumpFormat::Text):
                break;
            default:
                break;
        }
    }

    ApiDumpFormat format() const { return output_format; }

    void formatNameType(int indents, const char *name, const char *type) const {
        output_stream << indentation(indents) << name << ": ";
        // We have to 'print' an empty string for the setw to actually add the desired padding.
        if (use_spaces)
            output_stream << std::setw(name_size - (int)strlen(name) - 2) << "";
        else
            output_stream << std::setw((name_size - (int)strlen(name) - 3 + tab_size) / tab_size) << "";

        if (show_type) {
            if (use_spaces)
                output_stream << std::left << std::setw(type_size) << type << " = ";
            else
                output_stream << type << std::setw((type_size - (int)strlen(type) - 1 + tab_size) / tab_size) << "" << " = ";
        } else {
            output_stream << " = ";
        }
    }

    // The padding for `indents` levels, as a view the caller streams itself: a run of the indent
    // character from a cache, rather than a setw-padded empty string written to the stream as a side
    // effect, which cost two stream operations (and a stream sentry each) for every key and brace even
    // when indent_size is 0. The view is only valid until the next call, which is how every caller
    // uses it: streamed straight away, left to right.
    std::string_view indentation(int indents) const {
        const long long count = static_cast<long long>(indents) * indent_size;
        if (count <= 0) {
            return std::string_view();
        }
        const size_t length = static_cast<size_t>(count);
        const char fill = use_spaces ? ' ' : '\t';
        if (indent_cache.size() < length || indent_cache.front() != fill) {
            indent_cache.assign(std::max(length, indent_cache.size()), fill);
        }
        return std::string_view(indent_cache.data(), length);
    }

    bool shouldFlush() const { return should_flush; }

    bool shouldPreDump() const { return should_pre_dump; }

    bool showAddress() const { return show_address; }

    bool showParams() const { return show_params; }

    bool showShader() const { return show_shader; }

    bool showType() const { return show_type; }

    bool showTimestamp() const { return show_timestamp; }

    bool showThreadAndFrame() const { return show_thread_and_frame; }

    bool showCommandNumbers() const { return show_command_numbers; }

    // The const cast is necessary because everyone who 'writes' to the stream necessarily must be able to modify it.
    // Since basically every function in this struct is const, we have to work around that.
    std::ostream &stream() const { return output_stream; }

    // Whether the given frame should be dumped. Every caller must go through here rather than
    // reaching for condFrameOutput directly, otherwise the trigger and the range can disagree and
    // the per-frame markup stops matching the calls it wraps. Also the one place, along with
    // wasPreviousFrameDumped/isFrameRecorded/wasPreviousFrameRecorded below, that has to check
    // capture_process_name: everything else that decides whether to write anything is built on one
    // of these four.
    bool isFrameInRange(uint64_t frame) const {
        if (!process_matches_capture_name) {
            return false;
        }
        if (use_capture_trigger) {
            return capture_triggered.load(std::memory_order_relaxed);
        }
        return condFrameOutput.isFrameInRange(frame);
    }

    // Whether the frame before `frame` was dumped, i.e. whether there is open markup to close.
    // Under a trigger this cannot be derived from the current state: the trigger being off now is
    // exactly the case where the previous frame was dumped and still needs closing.
    bool wasPreviousFrameDumped(uint64_t frame) const {
        if (!process_matches_capture_name) {
            return false;
        }
        if (use_capture_trigger) {
            return previous_frame_dumped.load(std::memory_order_relaxed);
        }
        return condFrameOutput.isFrameInRange(frame - 1);
    }

    bool usingCaptureTrigger() const { return use_capture_trigger; }

    // What the frame counter tracked by output_range/output_range_queue_submits and capture_trigger
    // is actually counting; see notifyQueueSubmit/nextFrame.
    ApiDumpCaptureBoundary captureBoundary() const { return capture_trigger_boundary; }

    // Whether capture_process_name allows this process to be dumped; see init.
    bool processMatchesCaptureName() const { return process_matches_capture_name; }

    bool alwaysDumpSetup() const { return process_matches_capture_name && always_dump_setup; }
    bool showEnumValue() const { return show_enum_value; }
    int floatPrecision() const { return float_precision; }

    // Whether a frame object is written for this frame at all, as opposed to whether the frame's
    // calls are dumped. With always_dump_setup a setup command can occur in any frame, including
    // frames outside the captured range, and it needs a frame object to live in or the document is
    // malformed. Such a frame simply ends up holding only its setup commands, or none at all.
    //
    // Checks capture_process_name directly rather than only through isFrameInRange/alwaysDumpSetup:
    // this ORs the raw always_dump_setup member, not the gated accessor, so it needs its own check.
    bool isFrameRecorded(uint64_t frame) const {
        return process_matches_capture_name && (always_dump_setup || isFrameInRange(frame));
    }

    bool wasPreviousFrameRecorded(uint64_t frame) const {
        return process_matches_capture_name && (always_dump_setup || wasPreviousFrameDumped(frame));
    }

    // Whether a command is one the captured frames depend on to be interpretable.
    //
    // Calls inside the range refer to objects by handle, and a handle on its own says nothing about
    // what it refers to. Dumping the commands that create, destroy or record those objects - even
    // from outside the range - is what lets a reader resolve them.
    //
    // Classified by name because that is what the entry points have to hand. The prefixes cover
    // instance and device lifetime (vkCreateInstance and friends are all vkCreate/vkDestroy) as
    // well as general object lifetime and command buffer recording; the named commands are the
    // handle producers that do not follow the vkCreate/vkAllocate naming, and are where to add any
    // that turn out to be missing.
    static bool isSetupCommand(const char *funcName) {
        static const char *const kSetupPrefixes[] = {
            "vkCreate",    // object creation, including vkCreateInstance and vkCreateDevice
            "vkDestroy",   // object destruction, including vkDestroyInstance and vkDestroyDevice
            "vkAllocate",  // vkAllocateMemory, vkAllocateCommandBuffers, vkAllocateDescriptorSets
            "vkFree",      // vkFreeMemory, vkFreeCommandBuffers, vkFreeDescriptorSets
            "vkCmd",       // command buffer recording
        };
        for (const char *prefix : kSetupPrefixes) {
            if (strncmp(funcName, prefix, strlen(prefix)) == 0) {
                return true;
            }
        }

        static const char *const kSetupCommands[] = {
            // Command buffer recording, which vkCmd does not cover.
            "vkBeginCommandBuffer",
            "vkEndCommandBuffer",
            "vkResetCommandBuffer",
            "vkResetCommandPool",  // implicitly resets every command buffer in the pool
            // Handles that are produced rather than created.
            "vkGetDeviceQueue",
            "vkGetDeviceQueue2",
            "vkGetSwapchainImagesKHR",
            "vkEnumeratePhysicalDevices",
            "vkEnumeratePhysicalDeviceGroups",
            "vkEnumeratePhysicalDeviceGroupsKHR",
            "vkRegisterDeviceEventEXT",
            "vkRegisterDisplayEventEXT",
            // Display handles, which only exist off Android but cost nothing to cover.
            "vkGetDisplayPlaneSupportedDisplaysKHR",
            "vkGetDrmDisplayEXT",
            "vkGetRandROutputDisplayEXT",
            "vkGetWinrtDisplayNV",
            // Frees the pool's descriptor sets without naming them.
            "vkResetDescriptorPool",
        };
        for (const char *command : kSetupCommands) {
            if (strcmp(funcName, command) == 0) {
                return true;
            }
        }
        return false;
    }

    // vkQueueSubmit/vkQueueSubmit2/vkQueueSubmit2KHR - the commands that advance
    // queue_submit_count. Named distinctly from isSetupCommand above: these are never setup content
    // in Frames boundary mode, only in QueueSubmits mode, where a pre-range submission needs to be
    // force-dumped the same way any other setup command is - see setCurrentCommand and
    // dump_json_function_head's isSetupSubmission field.
    static bool isQueueSubmitFunction(const char *funcName) {
        return strcmp(funcName, "vkQueueSubmit") == 0 || strcmp(funcName, "vkQueueSubmit2") == 0 ||
               strcmp(funcName, "vkQueueSubmit2KHR") == 0;
    }

    // Re-read the trigger property. Called once per frame, before the frame's dump state is decided.
    //
    // The property is read outright each time rather than checked against __system_property_serial
    // first: that function only appears in the NDK headers from r28 on, and one property read per
    // frame is not worth a version dependency in a layer that serializes every call it dumps.
    void refreshCaptureTrigger() {
        if (!use_capture_trigger) {
            return;
        }
        previous_frame_dumped.store(capture_triggered.load(std::memory_order_relaxed), std::memory_order_relaxed);
#ifdef ANDROID
        if (capture_trigger_prop == nullptr) {
            return;
        }
        capture_triggered.store(readCaptureTriggerProp(), std::memory_order_relaxed);
#endif
    }

    void init(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator) {
        VkuLayerSettingSet layerSettingSet = VK_NULL_HANDLE;
        vkuCreateLayerSettingSet("VK_LAYER_LUNARG_api_dump", vkuFindLayerSettingsCreateInfo(pCreateInfo), pAllocator, nullptr,
                                 &layerSettingSet);

        vkuSetLayerSettingCompatibilityNamespace(layerSettingSet, GetDefaultPrefix());

        // Restricts dumping to a single named process:
        // an empty value (the default) means every process is captured, and
        // otherwise the match is an exact, case sensitive comparison against GetCurrentProcessName.
        // Checked early because a process this excludes must not open or write anything below -
        // see isFrameInRange, wasPreviousFrameDumped, isFrameRecorded, wasPreviousFrameRecorded, and
        // alwaysDumpSetup, which are what every dump decision ultimately goes through.
        std::string capture_process_name;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyCaptureProcessName)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyCaptureProcessName, capture_process_name);
        }
        process_matches_capture_name = capture_process_name.empty() || capture_process_name == GetCurrentProcessName();

        // How the output file is written, read here because it decides what the file is opened as.
        // async_write moves the file writes to a background thread behind a bounded in-memory buffer,
        // instead of the thread being dumped waiting on the file system. buffer_size_kb is the total
        // memory that buffer may use for the whole process (not per thread - every thread formats into
        // the same buffer, one at a time under the output mutex). Without async_write it instead sizes
        // the ordinary file stream's own buffer, so combine it with flush=false.
        bool async_write = false;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyAsyncWrite)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyAsyncWrite, async_write);
        }
        int buffer_size_kb = 0;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyBufferSizeKb)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyBufferSizeKb, buffer_size_kb);
            // Capped at the same limit the asynchronous buffer is clamped to, so a typo cannot ask for
            // an absurd allocation from either path.
            buffer_size_kb = std::min(std::max(buffer_size_kb, 0), static_cast<int>(ApiDumpAsyncFileBuf::kMaxBufferBytes / 1024));
        }

        // Read the format type first as it may be used in the output file extension
        output_format = ApiDumpFormat::Text;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyOutputFormat)) {
            std::string value;
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyOutputFormat, value);
            value = ToLowerString(value);
            if (value == "html") {
                output_format = ApiDumpFormat::Html;
            } else if (value == "json") {
                output_format = ApiDumpFormat::Json;
            } else {
                output_format = ApiDumpFormat::Text;
            }
        }

        // If the layer settings file has a flag indicating to output to a file,
        // do so, to the appropriate default filename.
        std::string filename_string = "";
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyFile)) {
            bool file = false;
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyFile, file);

            if (file) {
                if (output_format == ApiDumpFormat::Html) {
                    filename_string = "vk_apidump.html";
                } else if (output_format == ApiDumpFormat::Json) {
                    filename_string = "vk_apidump.json";
                } else {
                    filename_string = "vk_apidump.txt";
                }
            }
        }

        // Check if there is a specific filename that should be used
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyLogFilename)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyLogFilename, filename_string);
        }

        // Append file extension if one doesn't exist or is the wrong extension. Make sure the found extension is at the end
        if (!filename_string.empty()) {
            size_t txt_pos = filename_string.find(".txt", filename_string.size() - 4);
            size_t html_pos = filename_string.find(".html", filename_string.size() - 5);
            size_t json_pos = filename_string.find(".json", filename_string.size() - 5);

            if (output_format == ApiDumpFormat::Html) {
                if (json_pos != std::string::npos) filename_string.erase(json_pos);
                if (txt_pos != std::string::npos) filename_string.erase(txt_pos);
                if (html_pos == std::string::npos) filename_string.append(".html");
            } else if (output_format == ApiDumpFormat::Json) {
                if (html_pos != std::string::npos) filename_string.erase(html_pos);
                if (txt_pos != std::string::npos) filename_string.erase(txt_pos);
                if (json_pos == std::string::npos) filename_string.append(".json");
            } else {
                if (html_pos != std::string::npos) filename_string.erase(html_pos);
                if (json_pos != std::string::npos) filename_string.erase(json_pos);
                if (txt_pos == std::string::npos) filename_string.append(".txt");
            }
        }

        // If one of the above has set a filename, open the file as an output stream. Skipped for a
        // process capture_process_name excludes: the configured path is one fixed name shared by
        // every process the layer loads into, so every excluded process opening and truncating it
        // regardless would race the one process actually meant to write it.
        //
        // A process may create more than one VkInstance, which runs init again on this same settings
        // object, so an output that is already open is left as it is.
        if (!filename_string.empty() && process_matches_capture_name && !async_file_buf) {
            if (async_write && !output_file_stream.is_open()) {
                const size_t buffer_bytes = static_cast<size_t>(buffer_size_kb > 0 ? buffer_size_kb : kDefaultAsyncBufferSizeKb) * 1024;
                async_file_buf = ApiDumpAsyncFileBuf::Open(filename_string, buffer_bytes);
                if (async_file_buf) {
                    output_stream.rdbuf(async_file_buf.get());
                }
                // Otherwise the file or the writer thread could not be created, so fall back to the
                // ordinary synchronous stream below rather than losing the dump.
            }
            if (!async_file_buf) {
                if (buffer_size_kb > 0 && !output_file_stream.is_open()) {
                    // Has to be given to the stream before the file is opened to take effect.
                    const size_t buffer_bytes = static_cast<size_t>(buffer_size_kb) * 1024;
                    output_file_buffer.reset(new char[buffer_bytes]);
                    output_file_stream.rdbuf()->pubsetbuf(output_file_buffer.get(), static_cast<std::streamsize>(buffer_bytes));
                }
                output_file_stream.open(filename_string, std::ofstream::out | std::ostream::trunc);
                output_stream.rdbuf(output_file_stream.rdbuf());
            }
        }

        show_params = true;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyDetailedOutput)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyDetailedOutput, show_params);
        }

        show_address = true;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyNoAddr)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyNoAddr, show_address);
            // must invert the setting since the setting is called "no address", which is the logical
            // opposite of show_address
            show_address = !show_address;
        }

        should_flush = true;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyFlush)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyFlush, should_flush);
        }
        // With async_write the data is handed to the writer at every frame and queue submission
        // boundary instead, and when another layer asks for it (vkFlushAPIDUMP). A flush per call
        // would only add a hand-off per call.
        if (async_file_buf) {
            should_flush = false;
        }

        should_pre_dump = false;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyPreDump)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyPreDump, should_pre_dump);
        }

        show_timestamp = false;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyTimestamp)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyTimestamp, show_timestamp);
        }

        indent_size = 4;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyIndentSize)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyIndentSize, indent_size);
            indent_size = std::max(indent_size, 0);
        }
        tab_size = indent_size;

        show_type = true;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyShowTypes)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyShowTypes, show_type);
        }

        name_size = 32;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyNameSize)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyNameSize, name_size);
            name_size = std::max(name_size, 0);
        }

        type_size = 0;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyTypeSize)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyTypeSize, type_size);
            type_size = std::max(type_size, 0);
        }

        use_spaces = true;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyUseSpaces)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyUseSpaces, use_spaces);
        }

        show_shader = false;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyShowShader)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyShowShader, show_shader);
        }

        show_thread_and_frame = true;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyShowThreadAndFrame)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyShowThreadAndFrame, show_thread_and_frame);
        }

        show_command_numbers = false;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyShowCommandNumbers)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyShowCommandNumbers, show_command_numbers);
        }

        // Whether output_range/output_range_queue_submits and capture_trigger count frames
        // (advanced by vkQueuePresentKHR) or queue submissions (advanced by vkQueueSubmit and
        // friends) - the latter needed for compute-only workloads that never present.
        capture_trigger_boundary = ApiDumpCaptureBoundary::Frames;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyCaptureTriggerBoundary)) {
            std::string boundary_value;
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyCaptureTriggerBoundary, boundary_value);
            if (ToLowerString(boundary_value) == "queue_submits") {
                capture_trigger_boundary = ApiDumpCaptureBoundary::QueueSubmits;
            }
        }

        // Only one boundary kind is ever active per process, so a single ConditionalFrameOutput is
        // reused for whichever range setting matches it rather than keeping two in sync.
        const char *output_range_key = capture_trigger_boundary == ApiDumpCaptureBoundary::QueueSubmits
                                            ? kSettingsKeyOutputRangeQueueSubmits
                                            : kSettingsKeyOutputRange;
        std::string cond_range_string;
        if (vkuHasLayerSetting(layerSettingSet, output_range_key)) {
            vkuGetLayerSettingValue(layerSettingSet, output_range_key, cond_range_string);
        }

        always_dump_setup = false;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyAlwaysDumpSetup)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyAlwaysDumpSetup, always_dump_setup);
        }

        show_enum_value = false;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyShowEnumValue)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyShowEnumValue, show_enum_value);
        }

        float_precision = 0;
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyFloatPrecision)) {
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyFloatPrecision, float_precision);
            float_precision = std::max(float_precision, 0);
        }

        // The trigger is opt-in by presence: when set, it governs dumping outright and output_range
        // is ignored. Callers pick one or the other, never both.
        if (vkuHasLayerSetting(layerSettingSet, kSettingsKeyCaptureTrigger)) {
            use_capture_trigger = true;
            bool initial_trigger = false;
            vkuGetLayerSettingValue(layerSettingSet, kSettingsKeyCaptureTrigger, initial_trigger);
            capture_triggered.store(initial_trigger, std::memory_order_relaxed);
            previous_frame_dumped.store(false, std::memory_order_relaxed);
#ifdef ANDROID
            capture_trigger_prop = __system_property_find(kCaptureTriggerProperty);
            if (capture_trigger_prop != nullptr) {
                capture_triggered.store(readCaptureTriggerProp(), std::memory_order_relaxed);
            }
#endif
        }

        if (cond_range_string == "" || cond_range_string == "0-0") {  //"0-0" is every frame, no need to check
            use_conditional_output = false;
        } else {
            bool parsingStatus = condFrameOutput.parseConditionalFrameRange(cond_range_string);
            if (!parsingStatus) {
                use_conditional_output = false;
            }
        }

        // Setfill stays active for the duration of the stream. Setting it during construction
        // means it doesn't have to be set again whenever setw() is called.
        output_stream << std::setfill(use_spaces ? ' ' : '\t');

        if (!use_spaces) {
            indent_size = 1;  // setting this allows indentation to not need a branch on use_spaces
        }

        // A process may create more than one VkInstance, which runs init again on this same settings
        // object. The document header and the opening frame belong to the output file rather than to
        // an instance, so writing them again would concatenate a second document into the same file
        // and leave it unparseable.
        if (document_opened) {
            vkuDestroyLayerSettingSet(layerSettingSet, pAllocator);
            return;
        }
        document_opened = true;

        // Generate HTML heading if specified. Skipped along with the file open above for a process
        // capture_process_name excludes, so it never writes a document header for a file another
        // process is meant to own.
        if (!process_matches_capture_name) {
            vkuDestroyLayerSettingSet(layerSettingSet, pAllocator);
            return;
        }

        if (output_format == ApiDumpFormat::Html) {
            // clang-format off
            // Insert html heading
            output_stream <<
                "<!doctype html>"
                "<html>"
                    "<head>"
                        "<title>Vulkan API Dump</title>"
                        "<style type='text/css'>"
                        "html {"
                            "background-color: #0b1e48;"
                            "background-image: url('https://vulkan.lunarg.com/img/bg-starfield.jpg');"
                            "background-position: center;"
                            "-webkit-background-size: cover;"
                            "-moz-background-size: cover;"
                            "-o-background-size: cover;"
                            "background-size: cover;"
                            "background-attachment: fixed;"
                            "background-repeat: no-repeat;"
                            "height: 100%;"
                        "}"
                        "#header {"
                            "z-index: -1;"
                        "}"
                        "#header>img {"
                            "position: absolute;"
                            "width: 160px;"
                            "margin-left: -280px;"
                            "top: -10px;"
                            "left: 50%;"
                        "}"
                        "#header>h1 {"
                            "font-family: Arial, 'Helvetica Neue', Helvetica, sans-serif;"
                            "font-size: 44px;"
                            "font-weight: 200;"
                            "text-shadow: 4px 4px 5px #000;"
                            "color: #eee;"
                            "position: absolute;"
                            "width: 400px;"
                            "margin-left: -80px;"
                            "top: 8px;"
                            "left: 50%;"
                        "}"
                        "body {"
                            "font-family: Consolas, monaco, monospace;"
                            "font-size: 14px;"
                            "line-height: 20px;"
                            "color: #eee;"
                            "height: 100%;"
                            "margin: 0;"
                            "overflow: hidden;"
                        "}"
                        "#wrapper {"
                            "background-color: rgba(0, 0, 0, 0.7);"
                            "border: 1px solid #446;"
                            "box-shadow: 0px 0px 10px #000;"
                            "padding: 8px 12px;"
                            "display: inline-block;"
                            "position: absolute;"
                            "top: 80px;"
                            "bottom: 25px;"
                            "left: 50px;"
                            "right: 50px;"
                            "overflow: auto;"
                        "}"
                        "details>*:not(summary) {"
                            "margin-left: 22px;"
                        "}"
                        "summary:only-child {"
                          "display: block;"
                          "padding-left: 15px;"
                        "}"
                        "details>summary:only-child::-webkit-details-marker {"
                            "display: none;"
                            "padding-left: 15px;"
                        "}"
                        ".var, .type, .val {"
                            "display: inline;"
                            "margin: 0 6px;"
                        "}"
                        ".type {"
                            "color: #acf;"
                        "}"
                        ".val {"
                            "color: #afa;"
                            "text-align: right;"
                        "}"
                        ".thd {"
                            "color: #888;"
                        "}"
                        ".cmd {"
                            "color: #888;"
                        "}"
                        ".time {"
                            "color: #888;"
                        "}"
                        "</style>"
                    "</head>"
                    "<body>"
                        "<div id='header'>"
                            "<img src='https://lunarg.com/wp-content/uploads/2016/02/LunarG-wReg-150.png' alt='LunarG Logo'/>"
                            "<h1>Vulkan API Dump</h1>"
                        "</div>"
                        "<div id='wrapper'>";
            // clang-format on
        } else if (output_format == ApiDumpFormat::Json) {
            output_stream << "[\n";
        }

        if (isFrameRecorded(0)) {
            setupInterFrameOutputFormatting(0);
        }

        vkuDestroyLayerSettingSet(layerSettingSet, pAllocator);
    }

   private:
#ifdef ANDROID
    // Read the already-resolved trigger property. Only called when the serial says it changed.
    bool readCaptureTriggerProp() const {
        bool value = false;
        __system_property_read_callback(
            capture_trigger_prop,
            [](void *cookie, const char *, const char *prop_value, uint32_t) {
                std::string lower = ToLowerString(prop_value);
                *static_cast<bool *>(cookie) = (lower == "true" || lower == "1");
            },
            &value);
        return value;
    }
#endif

    // Utility member to enable easier comparison by forcing a string to all lower-case
    static std::string ToLowerString(const std::string &value) {
        std::string lower_value = value;
        std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
        return lower_value;
    }

    // The mutable is necessary because everyone who 'writes' to the stream necessarily must be able to modify it.
    // Since basically every function in this struct is const, we have to work around that.
    mutable std::ostream output_stream;
    // The buffer handed to output_file_stream when buffer_size_kb sizes it. Declared before the stream
    // so it is destroyed after it.
    std::unique_ptr<char[]> output_file_buffer;
    std::ofstream output_file_stream;
    // Set instead of output_file_stream when async_write is on; see ApiDumpAsyncFileBuf.
    std::unique_ptr<ApiDumpAsyncFileBuf> async_file_buf;
    // The padding indentation() hands out views of. Written from a const method, like output_stream.
    mutable std::string indent_cache;
#ifdef __ANDROID__
    std::unique_ptr<AndroidLogcatBuf<>> android_logcat_buf = nullptr;
#endif
    ApiDumpFormat output_format;
    bool show_params;
    bool show_address;
    bool should_flush;
    bool should_pre_dump;
    bool show_timestamp;

    bool show_type;
    int indent_size;  // how many indent levels to use - also sets the tab_size
    int name_size;
    int type_size;
    bool use_spaces;
    bool show_shader;
    bool show_thread_and_frame;
    bool show_command_numbers;

    bool use_conditional_output = false;
    ConditionalFrameOutput condFrameOutput;
    ApiDumpCaptureBoundary capture_trigger_boundary = ApiDumpCaptureBoundary::Frames;

    // Whether this process is the one capture_process_name names, or that setting is empty (every
    // process matches). Defaults to true (no filter) so that reading this before init runs - not
    // currently possible, but every other flag here follows the same default-open convention -
    // behaves the same as an absent setting rather than silently disabling capture.
    bool process_matches_capture_name = true;

    // When the capture_trigger setting is present, the trigger replaces the output_range check
    // entirely rather than combining with it. An output_range of "" or "0-0" means "every frame"
    // (see init), so there is no range value that could express "nothing until triggered".
    // Whether the document header has already been written to the output file. See init.
    bool document_opened = false;
    bool always_dump_setup = false;
    bool show_enum_value = false;
    int float_precision = 0;
    bool use_capture_trigger = false;
    std::atomic<bool> capture_triggered{false};
    // State the trigger had while the previous frame was being dumped. setupInterFrameOutputFormatting
    // has to close the markup it opened for that frame, which is only correct if it is told what was
    // actually emitted then rather than what the trigger says now.
    std::atomic<bool> previous_frame_dumped{false};
#ifdef ANDROID
    // Resolved once, so that the per frame refresh only pays for reading the value and not for
    // looking the property up by name again.
    const prop_info *capture_trigger_prop = nullptr;
#endif

    int tab_size;  // equal to the indent size if using spaces, otherwise is equal to 1
};

class ApiDumpInstance {
   public:
    ApiDumpInstance() noexcept : frame_count(0), queue_submit_count(0), command_count(0) {
        program_start = std::chrono::system_clock::now();
    }
    // Can't copy or move this type
    ApiDumpInstance(const ApiDumpInstance &) = delete;
    ApiDumpInstance &operator=(const ApiDumpInstance &) = delete;
    ApiDumpInstance(ApiDumpInstance &&) = delete;
    ApiDumpInstance &operator=(ApiDumpInstance &&) = delete;

    ~ApiDumpInstance() {
        // Closing depends on whether a frame object was opened, not on whether anything was dumped
        // into it. A recorded frame that ends up empty - which always_dump_setup makes common, since
        // every frame is recorded but most hold no setup commands - still has markup to close.
        if (settings().isFrameRecorded(frame_count)) settings().closeFrameOutput();
    }

    void initLayerSettings(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator) {
        this->dump_settings.init(pCreateInfo, pAllocator);
    }

    uint64_t frameCount() {
        std::lock_guard<std::mutex> lg(frame_mutex);
        uint64_t count = frame_count;
        return count;
    }

    // Independent of frameCount - see notifyQueueSubmit. Exposed mainly for tests.
    uint64_t queueSubmitCount() {
        std::lock_guard<std::mutex> lg(frame_mutex);
        uint64_t count = queue_submit_count;
        return count;
    }

    // Called from vkQueuePresentKHR, from vkQueueSubmit/vkQueueSubmit2 when a chained
    // VkFrameBoundaryEXT marks the end of a frame (see checkFrameBoundaryInSubmit), and from
    // vkFrameBoundaryANDROID. Always advances frame_count and the frame's own JSON markup,
    // regardless of capture_trigger_boundary - a frame boundary is a frame boundary independent of
    // what output_range/capture_trigger is currently counting. See notifyQueueSubmit for the
    // independent counter used for range/trigger decisions when the boundary is queue_submits.
    void nextFrame() {
        std::lock_guard<std::mutex> lg(frame_mutex);
        ++frame_count;
        settings().setupInterFrameOutputFormatting(frame_count);
        first_func_call_on_frame = true;
        if (settings().captureBoundary() == ApiDumpCaptureBoundary::Frames) {
            refreshShouldDumpOutput(frame_count);
        }
        // Everything written for the frame that just ended is complete, so give it to the writer. This
        // also covers the capture trigger turning off, which refreshShouldDumpOutput only notices here.
        settings().handOffOutput();
    }

    // Called from every vkQueueSubmit/vkQueueSubmit2/vkQueueSubmit2KHR. Always advances
    // queue_submit_count, regardless of capture_trigger_boundary - mirrors frame_count always
    // advancing on real frame boundaries no matter what the active boundary is. Only feeds the
    // dump-eligibility decision (should_dump_output) when the layer is configured to count queue
    // submissions instead of frames; never touches the frame's own JSON markup - see nextFrame for
    // that, driven independently by real frame-boundary signals only.
    void notifyQueueSubmit() {
        std::lock_guard<std::mutex> lg(frame_mutex);
        ++queue_submit_count;
        if (settings().captureBoundary() == ApiDumpCaptureBoundary::QueueSubmits) {
            refreshShouldDumpOutput(queue_submit_count);
            // The queue submissions are the boundary here, and with no frames to hand off at, a whole
            // capture can be a single frame object. Frames mode hands off in nextFrame instead, which
            // is cheaper than doing it for every submission.
            settings().handOffOutput();
        }
    }

    // Scans each submission for a chained VkFrameBoundaryEXT (VK_EXT_frame_boundary) marking the
    // end of a frame, advancing the real frame counter through nextFrame() when found - regardless
    // of capture_trigger_boundary. Called from vkQueueSubmit/vkQueueSubmit2/vkQueueSubmit2KHR
    // alongside notifyQueueSubmit, not instead of it: one counts frames, the other queue
    // submissions, independently.
    void checkFrameBoundaryInSubmit(uint32_t submitCount, const VkSubmitInfo *pSubmits) {
        for (uint32_t i = 0; i < submitCount; ++i) {
            if (IsFrameBoundaryEnd(pSubmits[i].pNext)) nextFrame();
        }
    }
    void checkFrameBoundaryInSubmit(uint32_t submitCount, const VkSubmitInfo2 *pSubmits) {
        for (uint32_t i = 0; i < submitCount; ++i) {
            if (IsFrameBoundaryEnd(pSubmits[i].pNext)) nextFrame();
        }
    }

    // VK_ANDROID_frame_boundary is not in the official registry, so there is no generated
    // dispatch-table slot to resolve the next layer/driver's real implementation through. Resolved
    // once at CreateDevice via a direct GetDeviceProcAddr call instead (see
    // api_dump_handwritten_functions.h) and stored here - the same pattern this codebase already
    // uses elsewhere for a function with no dispatch-table slot
    // (GetCommandNumberAPIDUMP via GetInstanceProcAddr).
    void setFrameBoundaryAndroidFunction(PFN_vkFrameBoundaryANDROID fp) { next_frame_boundary_android = fp; }

    // Hand-written interception for vkFrameBoundaryANDROID. Forwards to the resolved pointer so the
    // app's call still reaches the driver - it is void, so there is nothing to fail even if no
    // driver below actually implements it - then advances the real frame counter the same way any
    // other genuine frame-boundary signal does.
    void frameBoundaryAndroid(VkDevice device, VkSemaphore semaphore, VkImage image) {
        if (next_frame_boundary_android) next_frame_boundary_android(device, semaphore, image);
        nextFrame();
    }

    // The number assigned to the command currently being dumped. Only advanced in dump_function_head
    // once a command is actually written, so numbering stays contiguous within the dump rather than
    // counting commands that filtering left out. Guarded by the output mutex like setCurrentCommand,
    // for the same reason: every entry point holds it for its whole body.
    uint64_t commandCount() const { return command_count; }

    void nextCommand() { ++command_count; }

    // The commandNumber of the most recently dumped command, for other layers to poll via
    // vkGetCommandNumberAPIDUMP. commandCount() is the number the *next* dumped command will get,
    // so this is one behind it; before anything has been dumped it reads 0, same as commandCount().
    uint64_t lastCommandNumber() const { return command_count > 0 ? command_count - 1 : 0; }

    // Whether the current frame/queue submission is being dumped in its entirety - whichever of the
    // two output_range/capture_trigger is actually counting; see activeBoundaryCount.
    bool frameIsDumped() {
        // Under a trigger the answer changes while the app runs, so the one-shot latch below would
        // pin it to whatever was true before the first frame boundary.
        if (settings().usingCaptureTrigger()) {
            return should_dump_output;
        }
        if (!conditional_initialized) {
            should_dump_output = settings().isFrameInRange(activeBoundaryCount());
            conditional_initialized = true;
        }
        return should_dump_output;
    }

    // Record which command the calling entry point is, so that shouldDumpOutput can answer for that
    // command rather than only for the frame.
    //
    // Called from dump_function_head, which every entry point invokes before it dumps anything and
    // while holding the output mutex - hence a plain member rather than anything thread local, and
    // hence no need to thread the name through the generated dispatch's many call sites.
    void setCurrentCommand(const char *funcName) {
        // Classifying costs a few string compares, so it is skipped whenever it cannot change the
        // answer: with the setting off nothing extra is dumped, and inside the range everything is
        // dumped already.
        current_command_is_setup =
            settings().alwaysDumpSetup() && !frameIsDumped() &&
            (ApiDumpSettings::isSetupCommand(funcName) ||
             (settings().captureBoundary() == ApiDumpCaptureBoundary::QueueSubmits &&
              ApiDumpSettings::isQueueSubmitFunction(funcName)));
    }

    bool shouldDumpOutput() { return frameIsDumped() || current_command_is_setup; }

    // Whether the command currently being dumped is being force-dumped as setup content despite its
    // frame/queue submission not being in range - see setCurrentCommand. Distinct from
    // shouldDumpOutput(), which is also true for content that is in range on its own merits.
    bool currentCommandIsSetup() const { return current_command_is_setup; }

    bool firstFunctionCallOnFrame() {
        if (first_func_call_on_frame) {
            first_func_call_on_frame = false;
            return true;
        }
        return false;
    }

    std::mutex &outputMutex() { return output_mutex; }

    ApiDumpSettings &settings() { return dump_settings; }

    uint64_t threadID() {
        // A thread's number never changes once assigned, so it is remembered per thread: this runs for
        // every dumped call, and taking thread_mutex and searching thread_map each time added a lock
        // and a hash lookup to all of them. Keyed by the instance too, because the cache is shared by
        // every ApiDumpInstance a thread uses (tests create their own), and one instance's numbers
        // mean nothing to another. A serial rather than the address, since a destroyed instance's
        // address can be reused by the next one.
        thread_local uint64_t cached_serial = 0;
        thread_local uint64_t cached_id = 0;
        if (cached_serial == serial) {
            return cached_id;
        }

        std::thread::id this_id = std::this_thread::get_id();
        std::lock_guard<std::mutex> lg(thread_mutex);

        auto it = thread_map.find(this_id);
        if (it == thread_map.end()) {
            it = thread_map.insert({this_id, thread_map.size()}).first;
        }
        cached_serial = serial;
        cached_id = it->second;
        return cached_id;
    }

    void setCmdBuffer(VkCommandBuffer cmd_buffer) { this->cmd_buffer = cmd_buffer; }

    VkCommandBufferLevel getCmdBufferLevel() {
        std::lock_guard<std::mutex> lg(cmd_buffer_state_mutex);
        const auto level_iter = cmd_buffer_level.find(cmd_buffer);
        assert(level_iter != cmd_buffer_level.end());
        const auto level = level_iter->second;
        return level;
    }

    void eraseCmdBuffers(VkDevice device, VkCommandPool cmd_pool, std::vector<VkCommandBuffer> cmd_buffers) {
        cmd_buffers.erase(std::remove(cmd_buffers.begin(), cmd_buffers.end(), nullptr), cmd_buffers.end());
        if (!cmd_buffers.empty()) {
            std::lock_guard<std::mutex> lg(cmd_buffer_state_mutex);

            const auto pool_cmd_buffers_iter = cmd_buffer_pools.find(std::make_pair(device, cmd_pool));
            assert(pool_cmd_buffers_iter != cmd_buffer_pools.end());

            for (const auto cmd_buffer : cmd_buffers) {
                pool_cmd_buffers_iter->second.erase(cmd_buffer);

                assert(cmd_buffer_level.count(cmd_buffer) > 0);
                cmd_buffer_level.erase(cmd_buffer);
            }
        }
    }

    void addCmdBuffers(VkDevice device, VkCommandPool cmd_pool, std::vector<VkCommandBuffer> cmd_buffers,
                       VkCommandBufferLevel level) {
        std::lock_guard<std::mutex> lg(cmd_buffer_state_mutex);
        auto &pool_cmd_buffers = cmd_buffer_pools[std::make_pair(device, cmd_pool)];
        pool_cmd_buffers.insert(cmd_buffers.begin(), cmd_buffers.end());

        for (const auto cmd_buffer : cmd_buffers) {
            assert(cmd_buffer_level.count(cmd_buffer) == 0);
            cmd_buffer_level[cmd_buffer] = level;
        }
    }

    void eraseCmdBufferPool(VkDevice device, VkCommandPool cmd_pool) {
        if (cmd_pool != VK_NULL_HANDLE) {
            std::lock_guard<std::mutex> lg(cmd_buffer_state_mutex);

            const auto cmd_buffers_iter = cmd_buffer_pools.find(std::make_pair(device, cmd_pool));
            if (cmd_buffers_iter != cmd_buffer_pools.end()) {
                for (const auto cmd_buffer : cmd_buffers_iter->second) {
                    assert(cmd_buffer_level.count(cmd_buffer) > 0);
                    cmd_buffer_level.erase(cmd_buffer);
                }
                cmd_buffers_iter->second.clear();
            }
        }
    }

    void setIsDynamicScissor(bool is_dynamic_scissor) { this->is_dynamic_scissor = is_dynamic_scissor; }
    void setIsDynamicViewport(bool is_dynamic_viewport) { this->is_dynamic_viewport = is_dynamic_viewport; }
    bool getIsDynamicScissor() const { return is_dynamic_scissor; }
    bool getIsDynamicViewport() const { return is_dynamic_viewport; }
    void setMemoryHeapCount(uint32_t memory_heap_count) { this->memory_heap_count = memory_heap_count; }
    uint32_t getMemoryHeapCount() { return memory_heap_count; }
    void setDescriptorType(VkDescriptorType type) { this->descriptor_type = type; }
    VkDescriptorType getDescriptorType() { return this->descriptor_type; }
    void setIsGPLPreRasterOrFragmentShader(bool in) { this->GPLPreRasterOrFragmentShader = in; }
    bool getIsGPLPreRasterOrFragmentShader() { return this->GPLPreRasterOrFragmentShader; }
    void setIndirectExecutionSetInfoType(VkIndirectExecutionSetInfoTypeEXT type) { this->indirectExecutionSetInfoType = type; }
    VkIndirectExecutionSetInfoTypeEXT getIndirectExecutionSetInfoType() { return this->indirectExecutionSetInfoType; }
    void setIndirectCommandsLayoutToken(VkIndirectCommandsTokenTypeEXT type) { this->indirectCommandsLayoutToken = type; }
    VkIndirectCommandsTokenTypeEXT getIndirectCommandsLayoutToken() { return this->indirectCommandsLayoutToken; }
    void setDescriptorMappingSource(VkDescriptorMappingSourceEXT source) { this->descriptorMappingSource = source; }
    VkDescriptorMappingSourceEXT getDescriptorMappingSource() { return this->descriptorMappingSource; }

    void setSpsMaxSubLayersMinus1(uint8_t sps_max_sub_layers_minus1) {
        this->sps_max_sub_layers_minus1 = sps_max_sub_layers_minus1;
    }
    uint8_t getSpsMaxSubLayersMinus1() { return sps_max_sub_layers_minus1; }
    void setVpsMaxSubLayersMinus1(uint8_t vps_max_sub_layers_minus1) {
        this->vps_max_sub_layers_minus1 = vps_max_sub_layers_minus1;
    }
    uint8_t getVpsMaxSubLayersMinus1() { return vps_max_sub_layers_minus1; }
    void setIsInVps(bool is_in_vps) { this->is_in_vps = is_in_vps; }
    bool getIsInVps() { return is_in_vps; }

    std::chrono::microseconds current_time_since_start() {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(now - program_start);
    }

    static ApiDumpInstance &current() {
        // Because ApiDumpInstance is a static variable in a static function, there will only be one instance of it.
        // Additionally, the object will be constructed on the *first* call to current(), rather than at process startup time.
        static ApiDumpInstance current_instance;
        return current_instance;
    }

    std::unordered_map<uint64_t, std::string> object_name_map;

    void set_vk_instance(VkPhysicalDevice phys_dev, VkInstance instance) { vk_instance_map.insert({phys_dev, instance}); }
    VkInstance get_vk_instance(VkPhysicalDevice phys_dev) const {
        if (vk_instance_map.count(phys_dev) == 0) return VK_NULL_HANDLE;
        return vk_instance_map.at(phys_dev);
    }

    void update_object_name_map(const VkDebugMarkerObjectNameInfoEXT *pNameInfo) {
        if (pNameInfo->pObjectName)
            object_name_map[pNameInfo->object] = pNameInfo->pObjectName;
        else
            object_name_map.erase(pNameInfo->object);
    }
    void update_object_name_map(const VkDebugUtilsObjectNameInfoEXT *pNameInfo) {
        if (pNameInfo->pObjectName)
            object_name_map[pNameInfo->objectHandle] = pNameInfo->pObjectName;
        else
            object_name_map.erase(pNameInfo->objectHandle);
    }

   private:
    // Never 0, so a thread's zero-initialised cache can never match it. See threadID.
    static uint64_t nextSerial() {
        static std::atomic<uint64_t> counter{0};
        return ++counter;
    }
    const uint64_t serial = nextSerial();

    // Which of the two independent counters output_range/output_range_queue_submits and
    // capture_trigger are actually meant to consult, per capture_trigger_boundary. Never used for
    // the frame's own JSON markup, which always tracks frame_count regardless - see nextFrame.
    uint64_t activeBoundaryCount() {
        return settings().captureBoundary() == ApiDumpCaptureBoundary::QueueSubmits ? queue_submit_count : frame_count;
    }

    // Shared by nextFrame/notifyQueueSubmit, called with whichever counter capture_trigger_boundary
    // says is active, to decide whether content counted against it is currently in range. Every
    // call site that reaches here is already serialized via outputMutex (every generated dispatch
    // entry point that dumps output holds it for its whole body), so frame_mutex guards this state
    // against nothing more than that invariant ever slipping.
    //
    // Deliberately does not touch first_func_call_on_frame: that latch tracks whether a new JSON
    // array was just opened, which only nextFrame ever does - resetting it here too would drop the
    // separator before the first command dumped after a queue submission that was not also a frame
    // boundary, corrupting the JSON.
    void refreshShouldDumpOutput(uint64_t count) {
        settings().refreshCaptureTrigger();
        should_dump_output = settings().isFrameInRange(count);
        conditional_initialized = true;
    }

    ApiDumpSettings dump_settings;
    std::mutex output_mutex;
    std::mutex frame_mutex;
    uint64_t frame_count;
    // Independent of frame_count - advanced by every queue submission regardless of
    // capture_trigger_boundary, the same way frame_count is advanced by every real frame boundary
    // regardless of it. See nextFrame/notifyQueueSubmit.
    uint64_t queue_submit_count;
    uint64_t command_count;

    // The next layer/driver's real vkFrameBoundaryANDROID, resolved once at CreateDevice - see
    // setFrameBoundaryAndroidFunction/frameBoundaryAndroid. Null until then, and whenever nothing
    // below actually implements it.
    PFN_vkFrameBoundaryANDROID next_frame_boundary_android = nullptr;

    std::mutex thread_mutex;
    std::unordered_map<std::thread::id, uint64_t> thread_map;

    std::mutex cmd_buffer_state_mutex;
    std::map<std::pair<VkDevice, VkCommandPool>, std::unordered_set<VkCommandBuffer>> cmd_buffer_pools;
    std::unordered_map<VkCommandBuffer, VkCommandBufferLevel> cmd_buffer_level;

    bool conditional_initialized = false;
    // This must default to false, matching capture_triggered's own default.
    bool should_dump_output = false;
    bool first_func_call_on_frame = true;
    // Whether the entry point currently executing is dumped despite its frame not being. Guarded by
    // the output mutex, which every entry point holds for its whole body. See setCurrentCommand.
    bool current_command_is_setup = false;

    std::chrono::system_clock::time_point program_start;

    // Store the VkInstance handle so we don't use null in the call to
    // vkGetInstanceProcAddr(instance_handle, "vkCreateDevice");
    std::unordered_map<VkPhysicalDevice, VkInstance> vk_instance_map;

    // Storage for getCmdBufferLevel() which is called in a place where it needs access to the cmd_buffer but it isn't present in
    // the current structure.
    VkCommandBuffer cmd_buffer;

    // Storage for VkPipelineViewportStateCreateInfo which needs to ignore the scissor and viewport pipeline state if their
    // respective dynamic state is set.
    bool is_dynamic_scissor;
    bool is_dynamic_viewport;

    // Storage for VkPhysicalDeviceMemoryBudgetPropertiesEXT which needs the number of heaps from VkPhysicalDeviceMemoryProperties
    uint32_t memory_heap_count;

    // Storage for the VkDescriptorDataEXT/VkResourceDescriptorDataEXT union to know what is the active element
    VkDescriptorType descriptor_type;

    // True when creating a graphics pipeline library with VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT or
    // VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT set in the VkGraphicsPipelineLibraryCreateInfoEXT struct.
    bool GPLPreRasterOrFragmentShader;

    // Storage for the VkIndirectExecutionSetInfoEXT union to know which is the active element
    VkIndirectExecutionSetInfoTypeEXT indirectExecutionSetInfoType;

    // Storage for the VkIndirectCommandsTokenDataEXT union to know which is the active element
    VkIndirectCommandsTokenTypeEXT indirectCommandsLayoutToken;

    // Storage for the VkDescriptorMappingSourceDataEXT union
    VkDescriptorMappingSourceEXT descriptorMappingSource;

    // Storage for StdVideoH265HrdParameters's array length for pSubLayerHrdParametersNal and pSubLayerHrdParametersVcl
    uint8_t sps_max_sub_layers_minus1;
    uint8_t vps_max_sub_layers_minus1;

    // True if StdVideoH265HrdParameters is currently in StdVideoH265VideoParameterSet, false if it is in
    // StdVideoH265SequenceParameterSetVui. Needed to determine whether to use vps_max_sub_layers_minus1 or
    // sps_max_sub_layers_minus1
    bool is_in_vps;
};

enum class OutputConstruct {
    pointer,
    value,
    api_struct,
    api_union,
};

// Helper function to determine the value of GPLPreRasterOrFragmentShader;
inline bool checkForGPLPreRasterOrFragmentShader(const VkGraphicsPipelineCreateInfo &object) {
    VkGraphicsPipelineLibraryFlagsEXT flags{};
    const VkBaseInStructure *pNext_chain = reinterpret_cast<const VkBaseInStructure *>(object.pNext);
    while (pNext_chain) {
        if (pNext_chain->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT) {
            const auto *gpl_create_info = reinterpret_cast<const VkGraphicsPipelineLibraryCreateInfoEXT *>(pNext_chain);
            flags = gpl_create_info->flags;
        }
        pNext_chain = reinterpret_cast<const VkBaseInStructure *>(pNext_chain->pNext);
    }
    return flags &
           (VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT | VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT);
}

//==================================== Templated Helpers ======================================//

inline void flush(const ApiDumpSettings &settings) {
    if (settings.shouldFlush()) {
        settings.stream().flush();
    }
}

template <ApiDumpFormat Format>
void dump_value_start(const ApiDumpSettings &settings) {
    if constexpr (Format == ApiDumpFormat::Html) settings.stream() << "<div class=\'val\'>";
    if constexpr (Format == ApiDumpFormat::Json) settings.stream() << " \"";
}

template <ApiDumpFormat Format>
void dump_value_end(const ApiDumpSettings &settings) {
    if constexpr (Format == ApiDumpFormat::Html) settings.stream() << "</div>";
    if constexpr (Format == ApiDumpFormat::Json) settings.stream() << '"';
}

// The Json dump is made of a great many tiny pieces, so it is written through an ApiDumpFastWriter,
// which gathers them and hands them to the stream's buffer in one call, instead of inserting each into
// the ostream. The *_w helpers below take the writer so that a caller writing several pieces - a whole
// field, say - uses one writer for all of them; the helpers without the suffix make a writer of their own.

// A value in Json: a quoted string of whatever is inserted. See dump_value for the float precision.
template <typename... T>
void dump_json_value_w(ApiDumpFastWriter &writer, const ApiDumpSettings &settings, T &&...values) {
    writer << " \"";
    if constexpr (sizeof...(T) == 1 && (std::is_floating_point_v<std::decay_t<T>> && ...)) {
        const int precision = settings.floatPrecision();
        if (precision > 0) {
            writer.setPrecision(precision);
        }
    }
    (writer << ... << values);
    writer << '"';
}

template <ApiDumpFormat Format, typename... T>
void dump_value(const ApiDumpSettings &settings, T &&...values) {
    if constexpr (Format == ApiDumpFormat::Json) {
        ApiDumpFastWriter writer(settings.stream());
        dump_json_value_w(writer, settings, values...);
    } else {
        dump_value_start<Format>(settings);
        // A lone floating point value is the only case worth widening: the default ostream precision
        // of 6 significant digits silently rounds, and a consumer that parses this file back cannot
        // recover the lost bits. Composite output is left alone so text and html are unaffected.
        if constexpr (sizeof...(T) == 1 && (std::is_floating_point_v<std::decay_t<T>> && ...)) {
            const int precision = settings.floatPrecision();
            if (precision > 0) {
                const std::streamsize previous = settings.stream().precision(precision);
                (settings.stream() << ... << values);
                settings.stream().precision(previous);
            } else {
                (settings.stream() << ... << values);
            }
        } else {
            (settings.stream() << ... << values);
        }
        dump_value_end<Format>(settings);
    }
}

template <ApiDumpFormat Format, typename... T>
void dump_value_hex(const ApiDumpSettings &settings, T &&...values) {
    if constexpr (Format == ApiDumpFormat::Json &&
                  ((std::is_integral_v<std::decay_t<T>> && !std::is_same_v<std::decay_t<T>, bool> &&
                    !std::is_same_v<std::decay_t<T>, char> && !std::is_same_v<std::decay_t<T>, signed char> &&
                    !std::is_same_v<std::decay_t<T>, unsigned char>) && ...)) {
        ApiDumpFastWriter writer(settings.stream());
        writer << " \"0x";
        (writer.hex(values), ...);
        writer << '"';
    } else {
        dump_value_start<Format>(settings);
        settings.stream() << "0x" << std::hex;
        (settings.stream() << ... << values);
        settings.stream() << std::dec;
        dump_value_end<Format>(settings);
    }
}

template <ApiDumpFormat Format>
void dump_string(const ApiDumpSettings &settings, const char *name) {
    dump_value<Format>(settings, name);
}

template <ApiDumpFormat Format, typename T>
void dump_enum_with_value(const ApiDumpSettings &settings, const char *name, T value) {
    dump_value<Format>(settings, name, " (", value, ")");
}

template <ApiDumpFormat Format, typename T>
void dump_enum(const ApiDumpSettings &settings, const char *name, T value) {
    // The number replaces the name rather than joining it. A consumer resolving names needs a
    // table generated from some particular set of Vulkan headers and cannot resolve an enumerant
    // newer than those, whereas the number is always exact.
    //
    // This applies to every output format. Leaving it off preserves what each format writes
    // today: name and value for text and html, name alone for json.
    if (settings.showEnumValue()) {
        dump_value<Format>(settings, value);
        return;
    }

    if constexpr (Format == ApiDumpFormat::Text || Format == ApiDumpFormat::Html) {
        dump_value<Format>(settings, name, " (", value, ")");
    } else if constexpr (Format == ApiDumpFormat::Json) {
        dump_value<Format>(settings, name);
    }
}

template <ApiDumpFormat Format>
void dump_pNext_struct_name(const void *object, const ApiDumpSettings &settings, const char *type_name, const char *var_name,
                            int indents);

template <ApiDumpFormat Format>
void dump_pNext_trampoline(const void *object, const ApiDumpSettings &settings, const char *type_name, const char *var_name,
                           int indents);

template <ApiDumpFormat Format>
void dump_pNext(const void *object, const ApiDumpSettings &settings, const char *type_string, const char *name, int indents) {
    if constexpr (Format == ApiDumpFormat::Text) {
        dump_pNext_struct_name<Format>(object, settings, type_string, name, indents);
    } else if constexpr (Format == ApiDumpFormat::Html || Format == ApiDumpFormat::Json) {
        dump_pNext_trampoline<Format>(object, settings, type_string, name, indents);
    }
}

// The address of a Json object. "address" stands in for it when it is null or when addresses are hidden.
inline void dump_json_address_w(ApiDumpFastWriter &writer, const ApiDumpSettings &settings, const void *address) {
    if (address != NULL && settings.showAddress()) {
        dump_json_value_w(writer, settings, address);
    } else {
        dump_json_value_w(writer, settings, "address");
    }
}

template <ApiDumpFormat Format>
void dump_address(const ApiDumpSettings &settings, const void *address) {
    if constexpr (Format == ApiDumpFormat::Json) {
        ApiDumpFastWriter writer(settings.stream());
        dump_json_address_w(writer, settings, address);
    } else {
        if (address == NULL) {
            // TEMPORARY to minimize diff in output while developing.
            dump_value<Format>(settings, "NULL");
        } else if (settings.showAddress())
            dump_value<Format>(settings, address);
        else
            dump_value<Format>(settings, "address");
    }
}

template <ApiDumpFormat Format>
void dump_separate_members(const ApiDumpSettings &settings) {
    if constexpr (Format == ApiDumpFormat::Json) {
        ApiDumpFastWriter writer(settings.stream());
        writer << ",\n";
    }
}

//============================== Json formatting helper functions ==============================//

// Each helper has a form ending in _w that writes through a writer the caller already has, so that a
// caller emitting several pieces - a whole field, say - gathers them all in one buffer. The plain forms
// make a writer of their own. Neither changes what is written.

inline void dump_json_key_start_array_w(ApiDumpFastWriter &writer, const ApiDumpSettings &settings, int indents, const char *key) {
    writer << settings.indentation(indents) << '"' << key << "\" :\n" << settings.indentation(indents) << "[\n";
}
inline void dump_json_end_array_w(ApiDumpFastWriter &writer, const ApiDumpSettings &settings, int indents) {
    writer << settings.indentation(indents) << ']';
}
inline void dump_json_newline_end_array_w(ApiDumpFastWriter &writer, const ApiDumpSettings &settings, int indents) {
    writer << '\n';
    dump_json_end_array_w(writer, settings, indents);
}
inline void dump_json_start_object_w(ApiDumpFastWriter &writer, const ApiDumpSettings &settings, int indents) {
    writer << settings.indentation(indents) << "{\n";
}
inline void dump_json_end_object_w(ApiDumpFastWriter &writer, const ApiDumpSettings &settings, int indents) {
    writer << '\n' << settings.indentation(indents) << '}';
}
inline void dump_json_key_w(ApiDumpFastWriter &writer, const ApiDumpSettings &settings, int indents, const char *key) {
    writer << settings.indentation(indents) << '"' << key << "\" :";
}

template <typename... T>
void dump_json_key_value_w(ApiDumpFastWriter &writer, const ApiDumpSettings &settings, int indents, const char *key, T &&...values) {
    dump_json_key_w(writer, settings, indents, key);
    dump_json_value_w(writer, settings, values...);
}

inline void dump_json_key_address_w(ApiDumpFastWriter &writer, const ApiDumpSettings &settings, int indents, const void *address) {
    dump_json_key_w(writer, settings, indents, "address");
    dump_json_address_w(writer, settings, address);
}

inline void dump_json_key_start_array(const ApiDumpSettings &settings, int indents, const char *key) {
    ApiDumpFastWriter writer(settings.stream());
    dump_json_key_start_array_w(writer, settings, indents, key);
}
inline void dump_json_end_array(const ApiDumpSettings &settings, int indents) {
    ApiDumpFastWriter writer(settings.stream());
    dump_json_end_array_w(writer, settings, indents);
}
inline void dump_json_newline_end_array(const ApiDumpSettings &settings, int indents) {
    ApiDumpFastWriter writer(settings.stream());
    dump_json_newline_end_array_w(writer, settings, indents);
}
inline void dump_json_start_object(const ApiDumpSettings &settings, int indents) {
    ApiDumpFastWriter writer(settings.stream());
    dump_json_start_object_w(writer, settings, indents);
}
inline void dump_json_end_object(const ApiDumpSettings &settings, int indents) {
    ApiDumpFastWriter writer(settings.stream());
    dump_json_end_object_w(writer, settings, indents);
}
inline void dump_json_key(const ApiDumpSettings &settings, int indents, const char *key) {
    ApiDumpFastWriter writer(settings.stream());
    dump_json_key_w(writer, settings, indents, key);
}

template <typename... T>
void dump_json_key_value(const ApiDumpSettings &settings, int indents, const char *key, T &&...values) {
    ApiDumpFastWriter writer(settings.stream());
    dump_json_key_value_w(writer, settings, indents, key, values...);
}

inline void dump_json_key_address(const ApiDumpSettings &settings, int indents, const void *address) {
    ApiDumpFastWriter writer(settings.stream());
    dump_json_key_address_w(writer, settings, indents, address);
}

// What follows an element of a Json array: a comma unless it is the last, and the newline.
inline void dump_json_element_end(const ApiDumpSettings &settings, bool last) {
    ApiDumpFastWriter writer(settings.stream());
    writer << (last ? "\n" : ",\n");
}

//================================ Common Output Functions ================================//

template <ApiDumpFormat Format>
void dump_start(const ApiDumpSettings &settings, OutputConstruct construct, const char *type_string, const char *name, int indents,
                const void *address = nullptr) {
    if constexpr (Format == ApiDumpFormat::Text) {
        settings.formatNameType(indents, name, type_string);
        if (construct == OutputConstruct::pointer) {
            dump_address<Format>(settings, address);
        } else if (construct == OutputConstruct::api_struct || construct == OutputConstruct::api_union) {
            if (settings.showAddress())
                dump_value<Format>(settings, address);
            else
                dump_value<Format>(settings, "address");
            if (construct == OutputConstruct::api_struct) {
                settings.stream() << ":\n";
            } else if (construct == OutputConstruct::api_union) {
                settings.stream() << " (Union):\n";
            }
        }

    } else if constexpr (Format == ApiDumpFormat::Html) {
        settings.stream() << "<details class='data'><summary>";
        settings.stream() << "<div class='var'>" << name << "</div>";
        if (settings.showType()) {
            settings.stream() << "<div class='type'>" << type_string << "</div>";
        }
        if (construct == OutputConstruct::pointer) {
            dump_address<Format>(settings, address);
        } else if (construct == OutputConstruct::api_struct) {
            if (settings.showAddress())
                dump_value<Format>(settings, address, "\n");
            else
                dump_value<Format>(settings, "address\n");
            settings.stream() << "</summary>";
        } else if (construct == OutputConstruct::api_union) {
            if (settings.showAddress())
                dump_value<Format>(settings, address, " (Union):\n");
            else
                dump_value<Format>(settings, "address (Union):\n");
            settings.stream() << "</summary>";
        }

    } else if constexpr (Format == ApiDumpFormat::Json) {
        // The whole of the opening of the object is one writer's worth, and so one call into the buffer.
        ApiDumpFastWriter writer(settings.stream());
        dump_json_start_object_w(writer, settings, indents);
        if (construct == OutputConstruct::api_union)
            dump_json_key_value_w(writer, settings, indents + 1, "type", type_string, " (Union)");
        else
            dump_json_key_value_w(writer, settings, indents + 1, "type", type_string);

        writer << ",\n";
        dump_json_key_value_w(writer, settings, indents + 1, "name", name);

        if (construct == OutputConstruct::pointer || address != nullptr) {
            writer << ",\n";
            dump_json_key_address_w(writer, settings, indents + 1, address);
        }
        if (construct != OutputConstruct::pointer) {
            writer << ",\n";
            if (construct == OutputConstruct::value) {
                dump_json_key_w(writer, settings, indents + 1, "value");
            } else if (construct == OutputConstruct::api_struct || construct == OutputConstruct::api_union) {
                dump_json_key_start_array_w(writer, settings, indents + 1, "members");
            }
        }
    }
}

template <ApiDumpFormat Format>
void dump_end(const ApiDumpSettings &settings, OutputConstruct construct, int indents) {
    if constexpr (Format == ApiDumpFormat::Text) {
        if (construct == OutputConstruct::value || construct == OutputConstruct::pointer) settings.stream() << "\n";

    } else if constexpr (Format == ApiDumpFormat::Html) {
        if (construct == OutputConstruct::value || construct == OutputConstruct::pointer) {
            settings.stream() << "</summary></details>";
        } else {
            settings.stream() << "</details>";
        }

    } else if constexpr (Format == ApiDumpFormat::Json) {
        ApiDumpFastWriter writer(settings.stream());
        if (construct == OutputConstruct::api_struct || construct == OutputConstruct::api_union) {
            dump_json_newline_end_array_w(writer, settings, indents + 1);
        }
        dump_json_end_object_w(writer, settings, indents);
    }
}

template <ApiDumpFormat Format>
void dump_nullptr(const ApiDumpSettings &settings, const char *type_string, const char *name, int indents) {
    dump_start<Format>(settings, OutputConstruct::pointer, type_string, name, indents);
    dump_end<Format>(settings, OutputConstruct::pointer, indents);
}

template <ApiDumpFormat Format>
void dump_char(const char *object, const ApiDumpSettings &settings, const char *type_string, const char *name, int indents,
               const void *address = nullptr) {
    dump_start<Format>(settings, OutputConstruct::value, type_string, name, indents, address);
    if constexpr (Format == ApiDumpFormat::Text || Format == ApiDumpFormat::Html) {
        if (object == NULL)
            dump_value<Format>(settings, "NULL");
        else
            dump_value<Format>(settings, '"', object, '"');
    } else if constexpr (Format == ApiDumpFormat::Json) {
        dump_value<Format>(settings, (object != NULL ? object : ""));
    }
    dump_end<Format>(settings, OutputConstruct::value, indents);
}

template <ApiDumpFormat Format>
void dump_void(const void *object, const ApiDumpSettings &settings, const char *type_string, const char *name, int indents,
               const void *address = nullptr) {
    dump_start<Format>(settings, OutputConstruct::pointer, type_string, name, indents, object);
    dump_end<Format>(settings, OutputConstruct::pointer, indents);
}

template <ApiDumpFormat Format>
void dump_api_version(uint32_t api_version, const ApiDumpSettings &settings, const char *type_string, const char *name, int indents,
                      const void *address = nullptr) {
    dump_start<Format>(settings, OutputConstruct::value, type_string, name, indents, address);
    dump_value<Format>(settings, api_version, " (", VK_API_VERSION_MAJOR(api_version), ".", VK_API_VERSION_MINOR(api_version), ".",
                       VK_API_VERSION_PATCH(api_version), ")");
    dump_end<Format>(settings, OutputConstruct::value, indents);
}

template <ApiDumpFormat Format>
void dump_special(const char *const &text, const ApiDumpSettings &settings, const char *type_string, const char *name,
                  int indents) {
    dump_start<Format>(settings, OutputConstruct::value, type_string, name, indents, text);
    dump_value<Format>(settings, text);
    dump_end<Format>(settings, OutputConstruct::value, indents);
}

template <ApiDumpFormat Format, typename T>
void dump_handle(T object, const ApiDumpSettings &settings, const char *type_string, const char *name, int indents,
                 const void *address = nullptr) {
    dump_start<Format>(settings, OutputConstruct::value, type_string, name, indents, address);
    if (settings.showAddress()) {
        if constexpr (Format == ApiDumpFormat::Text || Format == ApiDumpFormat::Html) {
            std::unordered_map<uint64_t, std::string>::const_iterator it =
                ApiDumpInstance::current().object_name_map.find((uint64_t)object);
            if (it != ApiDumpInstance::current().object_name_map.end()) {
                dump_value<Format>(settings, object, " [", it->second, "]");
            } else {
                dump_value<Format>(settings, object);
            }
        } else if constexpr (Format == ApiDumpFormat::Json) {
            dump_value<Format>(settings, object);
        }
    } else {
        dump_value<Format>(settings, "address");
    }
    dump_end<Format>(settings, OutputConstruct::value, indents);
}

template <ApiDumpFormat Format, typename T>
std::enable_if_t<!std::is_pointer_v<T>> dump_type(const T &object, const ApiDumpSettings &settings, const char *type_string,
                                                  const char *name, int indents, const void *address = nullptr) {
    dump_start<Format>(settings, OutputConstruct::value, type_string, name, indents, address);
    dump_value<Format>(settings, object);
    dump_end<Format>(settings, OutputConstruct::value, indents);
}

template <ApiDumpFormat Format, typename T>
std::enable_if_t<std::is_pointer_v<T>> dump_type(const T &object, const ApiDumpSettings &settings, const char *type_string,
                                                 const char *name, int indents, const void *address = nullptr) {
    dump_start<Format>(settings, OutputConstruct::pointer, type_string, name, indents, object);
    dump_end<Format>(settings, OutputConstruct::pointer, indents);
}

template <ApiDumpFormat Format, typename T>
std::enable_if_t<!std::is_pointer_v<T>> dump_type_hex(const T &object, const ApiDumpSettings &settings, const char *type_string,
                                                      const char *name, int indents, const void *address = nullptr) {
    dump_start<Format>(settings, OutputConstruct::value, type_string, name, indents, address);
    dump_value_hex<Format>(settings, object);
    dump_end<Format>(settings, OutputConstruct::value, indents);
}

template <ApiDumpFormat Format>
void dump_uint64_t_as_pointer(const uint64_t object, const ApiDumpSettings &settings, const char *type_string, const char *name,
                              int indents) {
    dump_start<Format>(settings, OutputConstruct::pointer, type_string, name, indents,
                       reinterpret_cast<const void *>(static_cast<uintptr_t>(object)));
    dump_end<Format>(settings, OutputConstruct::pointer, indents);
}

template <ApiDumpFormat Format, typename T, typename DumpValue>
void dump_pointer(const T *object, const ApiDumpSettings &settings, const char *type_string, const char *name, int indents,
                  DumpValue dump_value) {
    if (object == NULL) {
        dump_nullptr<Format>(settings, type_string, name, indents);
    } else {
        dump_value(*object, settings, type_string, name, indents, object);
    }
}

template <ApiDumpFormat Format>
void dump_array_start(const void *array, size_t len, const ApiDumpSettings &settings, const char *type_string, const char *name,
                      int indents) {
    if constexpr (Format == ApiDumpFormat::Text) {
        dump_start<Format>(settings, OutputConstruct::pointer, type_string, name, indents, array);
        settings.stream() << "\n";
    } else if constexpr (Format == ApiDumpFormat::Html) {
        dump_start<Format>(settings, OutputConstruct::api_struct, type_string, name, indents, array);
    } else if constexpr (Format == ApiDumpFormat::Json) {
        ApiDumpFastWriter writer(settings.stream());
        dump_json_start_object_w(writer, settings, indents);
        dump_json_key_value_w(writer, settings, indents + 1, "type", type_string);
        writer << ",\n";
        dump_json_key_value_w(writer, settings, indents + 1, "name", name);
        writer << ",\n";
        dump_json_key_address_w(writer, settings, indents + 1, array);
        if (len > 0 && array != NULL) {
            writer << ",\n";
            dump_json_key_start_array_w(writer, settings, indents + 1, "elements");
        } else {
            writer << '\n';
        }
    }
}

template <ApiDumpFormat Format>
void dump_array_end(const void *array, size_t len, const ApiDumpSettings &settings, int indents) {
    if constexpr (Format == ApiDumpFormat::Html) {
        dump_end<ApiDumpFormat::Html>(settings, OutputConstruct::api_struct, indents);
    } else if constexpr (Format == ApiDumpFormat::Json) {
        ApiDumpFastWriter writer(settings.stream());
        if (len > 0 && array != NULL) {
            dump_json_end_array_w(writer, settings, indents + 1);
        }
        dump_json_end_object_w(writer, settings, indents);
    }
}

// The name given to one element of an array: "[i]" in Json, "<name>[i]" in Text and Html (and the two
// index form for a double array). Built once per element, so it formats into a stack buffer instead of
// a std::stringstream and std::string, which allocated for every element of every array dumped. A name
// too long for the buffer - none of the generated ones are - falls back to a string, so the result is
// never truncated.
template <ApiDumpFormat Format>
class ArrayElementName {
   public:
    ArrayElementName(const char *name, size_t i) { build(prefixFor(name), i, nullptr); }
    ArrayElementName(const char *name, size_t i, size_t j) { build(prefixFor(name), i, &j); }

    const char *c_str() const { return overflow_.empty() ? buffer_ : overflow_.c_str(); }

   private:
    // Writes "<prefix>[i]", or "<prefix>[i][j]" when j is given, and a terminator. Assembled by hand
    // from the digits rather than by snprintf, which parses its format string on every element.
    void build(const char *prefix, size_t i, const size_t *j) {
        char first[24];
        char second[24];
        const size_t prefix_length = strlen(prefix);
        const size_t first_length = static_cast<size_t>(std::to_chars(first, first + sizeof(first), i).ptr - first);
        const size_t second_length = j ? static_cast<size_t>(std::to_chars(second, second + sizeof(second), *j).ptr - second) : 0;
        const size_t needed = prefix_length + 2 + first_length + (j ? 2 + second_length : 0);

        char *out = buffer_;
        if (needed >= sizeof(buffer_)) {
            overflow_.resize(needed + 1);
            out = &overflow_[0];
        }
        memcpy(out, prefix, prefix_length);
        out += prefix_length;
        *out++ = '[';
        memcpy(out, first, first_length);
        out += first_length;
        *out++ = ']';
        if (j) {
            *out++ = '[';
            memcpy(out, second, second_length);
            out += second_length;
            *out++ = ']';
        }
        *out = '\0';
    }

    static const char *prefixFor(const char *name) {
        if constexpr (Format == ApiDumpFormat::Text || Format == ApiDumpFormat::Html) {
            return name ? name : "";
        } else {
            return "";
        }
    }

    char buffer_[192];
    std::string overflow_;
};

template <ApiDumpFormat Format, size_t N, typename T, typename DumpElement>
void dump_double_array(const T(array)[][N], size_t len1, size_t len2, const ApiDumpSettings &settings, const char *type_string,
                       const char *name, const char *element_type, int indents, DumpElement dump_element) {
    if (len1 == 0 || len2 == 0) {
        return;
    }
    dump_array_start<Format>(array, len1 * len2, settings, type_string, name, indents);
    for (size_t i = 0; i < len1; ++i) {
        for (size_t j = 0; j < len2; ++j) {
            const ArrayElementName<Format> indexName(name, i, j);
            dump_element(array[i][j], settings, element_type, indexName.c_str(), indents + (Format == ApiDumpFormat::Json ? 2 : 1),
                         nullptr);
            if constexpr (Format == ApiDumpFormat::Json) {
                dump_json_element_end(settings, !(i < len1 - 1 && j < len2 - 1));
            }
        }
    }
    dump_array_end<Format>(array, len1 * len2, settings, indents);
}

template <ApiDumpFormat Format, size_t N, typename T, typename DumpElement>
void dump_single_array(const T (&array)[N], size_t len, const ApiDumpSettings &settings, const char *type_string, const char *name,
                       const char *element_type, int indents, DumpElement dump_element) {
    if (len == 0) {
        return;
    }
    dump_array_start<Format>(array, len, settings, type_string, name, indents);
    for (size_t i = 0; i < len; ++i) {
        const ArrayElementName<Format> indexName(name, i);
        dump_element(array[i], settings, element_type, indexName.c_str(), indents + (Format == ApiDumpFormat::Json ? 2 : 1),
                     nullptr);
        if constexpr (Format == ApiDumpFormat::Json) {
            dump_json_element_end(settings, !(i < len - 1));
        }
    }
    dump_array_end<Format>(array, len, settings, indents);
}

template <ApiDumpFormat Format, typename T, typename DumpElement>
void dump_pointer_array(const T *array, size_t len, const ApiDumpSettings &settings, const char *type_string, const char *name,
                        const char *element_type, int indents, DumpElement dump_element) {
    if (array == NULL || len == 0) {
        dump_nullptr<Format>(settings, type_string, name, indents);
        return;
    }
    dump_array_start<Format>(array, len, settings, type_string, name, indents);
    for (size_t i = 0; i < len; ++i) {
        const ArrayElementName<Format> indexName(name, i);
        dump_element(array[i], settings, element_type, indexName.c_str(), indents + (Format == ApiDumpFormat::Json ? 2 : 1),
                     array + i);
        if constexpr (Format == ApiDumpFormat::Json) {
            dump_json_element_end(settings, !(i < len - 1));
        }
    }
    dump_array_end<Format>(array, len, settings, indents);
}

template <ApiDumpFormat Format, typename T, typename DumpElement>
void dump_double_pointer_array(const T *const *array, size_t len, const ApiDumpSettings &settings, const char *type_string,
                               const char *name, const char *element_type, int indents, DumpElement dump_element) {
    if (array == nullptr || len == 0) {
        dump_nullptr<Format>(settings, type_string, name, indents);
        return;
    }
    dump_array_start<Format>(array, len, settings, type_string, name, indents);
    for (size_t i = 0; i < len; ++i) {
        const ArrayElementName<Format> indexName(name, i);
        dump_pointer<Format>(array[i], settings, element_type, indexName.c_str(), indents + (Format == ApiDumpFormat::Json ? 2 : 1),
                             dump_element);
        if constexpr (Format == ApiDumpFormat::Json) {
            dump_json_element_end(settings, !(i < len - 1));
        }
    }
    dump_array_end<Format>(array, len, settings, indents);
}

// Designed to be viewed in something like https://www.khronos.org/spir/visualizer/
// There is also now support in SPIRV-Tools (https://github.com/KhronosGroup/SPIRV-Tools/pull/5870) to consume the array
template <ApiDumpFormat Format, typename T>
void dump_spirv(const T *array, size_t len, const ApiDumpSettings &settings, const char *type_string, const char *name,
                int indents) {
    if (array == NULL || len == 0) {
        dump_nullptr<Format>(settings, type_string, name, indents);
        return;
    }
    dump_array_start<Format>(array, len, settings, type_string, name, indents);

    std::stringstream stream;
    // For JSON we will just dump it as a valid JSON array of string like
    // [ "0x07230203", "0x00010300", "0x0008000b", ...
    if constexpr (Format == ApiDumpFormat::Json) {
        for (size_t i = 0; i < len; ++i) {
            if (i != 0) {
                stream << ", ";
            }
            const uint32_t dword = static_cast<const uint32_t *>(array)[i];
            stream << "\"0x" << std::hex << std::setfill('0') << std::setw(8) << dword << "\"";
        }
        settings.stream() << stream.str();
    } else {
        stream << settings.indentation(indents) << "[ ";
        for (size_t i = 0; i < len; ++i) {
            if (i != 0) {
                stream << ", ";
            }
            const uint32_t dword = static_cast<const uint32_t *>(array)[i];
            stream << "0x" << std::hex << std::setfill('0') << std::setw(8) << dword;
        }
        stream << " ]\n";
        dump_value<Format>(settings, stream.str());
    }

    dump_array_end<Format>(array, len, settings, indents);
}

template <ApiDumpFormat Format>
void dump_before_pre_dump_formatting(const ApiDumpSettings &settings) {
    if constexpr (Format == ApiDumpFormat::Text) {
        if (ApiDumpInstance::current().settings().shouldPreDump()) {
            settings.stream() << "<before calling into implementation>\n";
        }
    }
}

template <ApiDumpFormat Format, typename T>
void dump_return_value(const ApiDumpSettings &settings, const char *returnType, T result) {
    if constexpr (Format == ApiDumpFormat::Text) {
        if (ApiDumpInstance::current().settings().shouldPreDump()) {
            settings.stream() << "return " << returnType;
        }
        settings.stream() << " ";

    } else if constexpr (Format == ApiDumpFormat::Json) {
        dump_separate_members<Format>(settings);
        dump_json_key(settings, 3, "returnValue");
    }
    dump_value<Format>(settings, result);
    if constexpr (Format == ApiDumpFormat::Text) {
        if (ApiDumpInstance::current().settings().shouldPreDump()) {
            settings.stream() << ":\n";
        }
    }
}

template <ApiDumpFormat Format, typename T, typename DumpReturnValue>
void dump_return_value(const ApiDumpSettings &settings, const char *returnType, T result, DumpReturnValue dump_return_value) {
    if constexpr (Format == ApiDumpFormat::Text) {
        if (ApiDumpInstance::current().settings().shouldPreDump()) {
            settings.stream() << "return " << returnType;
        }
        settings.stream() << " ";

    } else if constexpr (Format == ApiDumpFormat::Json) {
        dump_separate_members<Format>(settings);
        dump_json_key(settings, 3, "returnValue");
    }
    dump_return_value(result, settings);
    if constexpr (Format == ApiDumpFormat::Text) {
        if (ApiDumpInstance::current().settings().shouldPreDump()) {
            settings.stream() << ":\n";
        }
    }
}

template <ApiDumpFormat Format>
void dump_pre_params_formatting(const ApiDumpSettings &settings) {
    if constexpr (Format == ApiDumpFormat::Json) {
        dump_separate_members<Format>(settings);
        dump_json_key_start_array(settings, 3, "args");
    }
}
template <ApiDumpFormat Format>
void dump_post_params_formatting(const ApiDumpSettings &settings) {
    if constexpr (Format == ApiDumpFormat::Text || Format == ApiDumpFormat::Html) {
        settings.stream() << "\n";
    } else if constexpr (Format == ApiDumpFormat::Json) {
        ApiDumpFastWriter writer(settings.stream());
        writer << '\n' << settings.indentation(3) << "]\n";
    }
}

template <ApiDumpFormat Format>
void dump_pre_function_formatting(const ApiDumpSettings &settings) {
    if constexpr (Format == ApiDumpFormat::Text) {
        if (ApiDumpInstance::current().settings().shouldPreDump()) {
            settings.stream() << "<after returning from implementation>\n";
        } else {
            settings.stream() << ":\n";
        }
    } else if constexpr (Format == ApiDumpFormat::Html) {
        settings.stream() << "</summary>";
    }
}
template <ApiDumpFormat Format>
void dump_post_function_formatting(const ApiDumpSettings &settings) {
    if constexpr (Format == ApiDumpFormat::Html) {
        settings.stream() << "</details>";
    } else if constexpr (Format == ApiDumpFormat::Json) {
        ApiDumpFastWriter writer(settings.stream());
        writer << settings.indentation(2) << '}';
    }
}

//==================================== Function Head Helpers ======================================//

inline void dump_text_function_head(ApiDumpInstance &dump_inst, const char *funcName, const char *funcNamedParams,
                                    const char *funcReturn) {
    const ApiDumpSettings &settings(dump_inst.settings());
    bool wrote_header = false;
    if (settings.showThreadAndFrame()) {
        settings.stream() << "Thread " << dump_inst.threadID() << ", Frame " << dump_inst.frameCount();
        wrote_header = true;
    }
    if (settings.showCommandNumbers()) {
        if (wrote_header) settings.stream() << ", ";
        settings.stream() << "Command " << dump_inst.commandCount();
        wrote_header = true;
    }
    if (settings.showTimestamp()) {
        if (wrote_header) settings.stream() << ", ";
        settings.stream() << "Time " << dump_inst.current_time_since_start().count() << " us";
        wrote_header = true;
    }
    if (wrote_header) {
        settings.stream() << ":\n";
    }
    settings.stream() << funcName << "(" << funcNamedParams << ") returns " << funcReturn;
    if (ApiDumpInstance::current().settings().shouldPreDump() && ApiDumpInstance::current().shouldDumpOutput()) {
        settings.stream() << ":\n";
    }
    flush(settings);
}

inline void dump_html_function_head(ApiDumpInstance &dump_inst, const char *funcName, const char *funcNamedParams,
                                    const char *funcReturn) {
    const ApiDumpSettings &settings(dump_inst.settings());
    if (settings.showThreadAndFrame()) {
        settings.stream() << "<div class='thd'>Thread: " << dump_inst.threadID() << "</div>";
    }
    if (settings.showCommandNumbers()) {
        settings.stream() << "<div class='cmd'>Command: " << dump_inst.commandCount() << "</div>";
    }
    if (settings.showTimestamp())
        settings.stream() << "<div class='time'>Time: " << dump_inst.current_time_since_start().count() << " us</div>";
    settings.stream() << "<details class='fn'><summary>";
    settings.stream() << "<div class='var'>" << funcName << "(" << funcNamedParams << ")</div>";
    if (settings.showType()) {
        settings.stream() << "<div class='type'>" << funcReturn << "</div>";
    }
    flush(settings);
}

inline void dump_json_function_head(ApiDumpInstance &dump_inst, const char *funcName, const char *funcReturn) {
    const ApiDumpSettings &settings(dump_inst.settings());

    if (!dump_inst.firstFunctionCallOnFrame()) {
        dump_separate_members<ApiDumpFormat::Json>(settings);
    }
    // Display api call name
    dump_json_start_object(settings, 2);
    dump_json_key_value(settings, 3, "name", funcName);

    // Display command number
    if (settings.showCommandNumbers()) {
        dump_separate_members<ApiDumpFormat::Json>(settings);
        dump_json_key_value(settings, 3, "commandNumber", dump_inst.commandCount());
    }

    // Display thread info
    if (settings.showThreadAndFrame()) {
        dump_separate_members<ApiDumpFormat::Json>(settings);
        dump_json_key_value(settings, 3, "thread", "Thread ", dump_inst.threadID());
    }

    // Display elapsed time
    if (settings.showTimestamp()) {
        dump_separate_members<ApiDumpFormat::Json>(settings);
        dump_json_key_value(settings, 3, "time", dump_inst.current_time_since_start().count(), " us");
    }

    // Display return type
    dump_separate_members<ApiDumpFormat::Json>(settings);
    dump_json_key_value(settings, 3, "returnType", funcReturn);

    // Display whether this queue submission predates the active queue-submission range/trigger.
    // Written as a bare boolean, and only when true, matching isSetupFrame's own convention (a
    // native JSON value the reader's boolean() SAX callback expects, not the quoted-string
    // convention dump_json_key_value gives every other field here) - left out entirely for a
    // submission that is genuinely in range, rather than writing false. Only meaningful - and only
    // ever written - when the layer is counting queue submissions rather than frames, since a
    // submission's own frame in Frames boundary mode is not what output_range/capture_trigger is
    // deciding for it. See ApiDumpInstance::setCurrentCommand.
    if (settings.captureBoundary() == ApiDumpCaptureBoundary::QueueSubmits && ApiDumpSettings::isQueueSubmitFunction(funcName) &&
        dump_inst.currentCommandIsSetup()) {
        dump_separate_members<ApiDumpFormat::Json>(settings);
        dump_json_key(settings, 3, "isSetupSubmission");
        settings.stream() << " true";
    }
    flush(settings);
}

inline void dump_json_UNUSED(const ApiDumpSettings &settings, const char *type_string, const char *name, int indents) {
    ApiDumpFastWriter writer(settings.stream());
    dump_json_start_object_w(writer, settings, indents);
    dump_json_key_value_w(writer, settings, indents + 1, "type", type_string);
    writer << ",\n";
    dump_json_key_value_w(writer, settings, indents + 1, "name", name);
    writer << ",\n";
    dump_json_key_value_w(writer, settings, indents + 1, "address", "UNUSED");
    writer << ",\n";
    dump_json_key_value_w(writer, settings, indents + 1, "value", "UNUSED");
    dump_json_end_object_w(writer, settings, indents);
}

//==================================== Common Helpers ======================================//

inline void dump_function_head(ApiDumpInstance &dump_inst, const char *funcName, const char *funcNamedParams,
                               const char *funcReturn) {
    dump_inst.setCurrentCommand(funcName);
    if (dump_inst.shouldDumpOutput()) {
        switch (dump_inst.settings().format()) {
            case ApiDumpFormat::Text:
                dump_text_function_head(dump_inst, funcName, funcNamedParams, funcReturn);
                break;
            case ApiDumpFormat::Html:
                dump_html_function_head(dump_inst, funcName, funcNamedParams, funcReturn);
                break;
            case ApiDumpFormat::Json:
                dump_json_function_head(dump_inst, funcName, funcReturn);
                break;
        }
        dump_inst.nextCommand();
    }
}

//==================================== Exposed Query Functions ======================================//

// Lets other layers poll the commandNumber of the last command api_dump wrote, to correlate their
// own records against a specific entry in the dump.
// not a real Vulkan command, so it is resolved only through vkGetInstanceProcAddr and queried by
// name without the leading "vk" - "GetCommandNumberAPIDUMP" - so a caller cannot mistake it for one.
inline VKAPI_ATTR uint64_t VKAPI_CALL vkGetCommandNumberAPIDUMP() { return ApiDumpInstance::current().lastCommandNumber(); }

// Lets another layer make sure everything dumped so far is in the output file before it ends the
// process - with async_write the dump is otherwise partly held in memory, and a process that is killed
// takes that with it. Resolved the same way as the function above, as "FlushAPIDUMP".
//
// Waits up to timeout_ms, which covers both getting the output mutex (another thread may be inside a
// long driver call holding it) and the background writer finishing. Returns VK_TRUE if everything
// reached the file, VK_FALSE if time ran out or a write failed.
//
// Only flushes. It does not close the frame or the document, so the output is exactly what it would be
// at that point without the call, and calling it more than once is harmless. Must not be called from a
// thread that is inside one of this layer's own entry points, which already holds the output mutex.
inline VKAPI_ATTR VkBool32 VKAPI_CALL vkFlushAPIDUMP(uint32_t timeout_ms) {
    ApiDumpInstance &instance = ApiDumpInstance::current();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    std::unique_lock<std::mutex> lock(instance.outputMutex(), std::defer_lock);
    while (!lock.try_lock()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return VK_FALSE;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    return instance.settings().drainOutput(std::max(remaining, std::chrono::milliseconds(0))) ? VK_TRUE : VK_FALSE;
}
