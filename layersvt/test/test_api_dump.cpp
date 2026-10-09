/*
 * Copyright (c) 2023 The Khronos Group Inc.
 * Copyright (c) 2023 Valve Corporation
 * Copyright (c) 2023 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Author: Christophe Riccio <christophe@lunarg.com>
 */

#include "layer_test_helper.h"

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_beta.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <filesystem>

// Included directly (rather than only loaded as a layer) to unit test the pure range/boundary
// logic - ConditionalFrameOutput and ApiDumpSettings::captureBoundary - without needing a device.
#include "../api_dump.h"

static const char* kLayerName = "VK_LAYER_LUNARG_api_dump";

class ApiDumpTests : public VkTestFramework {
   public:
    ~ApiDumpTests(){};

    static void SetUpTestSuite() {}
    static void TearDownTestSuite(){};
};

TEST_F(ApiDumpTests, init_layer) {
    TEST_DESCRIPTION("Test Creating a Vulkan Instance with a layer");

    VkBool32 use_file = VK_TRUE;
    const char* filename_string = "api_dump_output.html";
    const char* output_format = "html";

    const std::vector<VkLayerSettingEXT> settings = {
        {kLayerName, "file", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &use_file},
        {kLayerName, "log_filename", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &filename_string},
        {kLayerName, "output_format", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &output_format}};

    layer_test::VulkanInstanceBuilder inst_builder;
    VkResult err = inst_builder.Init(settings);
    EXPECT_EQ(err, VK_SUCCESS);

    // check the output file is generated
    const std::filesystem::path path = std::filesystem::current_path() / std::filesystem::path(filename_string);
    FILE* file = fopen(path.string().c_str(), "r");
    ASSERT_TRUE(file != NULL);

    const char* file_start_content_expected = "<!doctype html>";
    std::string file_start_content_read;
    file_start_content_read.resize(std::strlen(file_start_content_expected));

    size_t fread_return = fread(&file_start_content_read[0], 1, file_start_content_read.size(), file);
    EXPECT_EQ(fread_return, file_start_content_read.size());
    fclose(file);

    EXPECT_STREQ(file_start_content_read.c_str(), file_start_content_expected);
}

// ConditionalFrameOutput parses output_range/output_range_queue_submits strings and answers
// isFrameInRange for them. Neither needs a Vulkan device, so these test it directly rather than
// through a full capture.
TEST(ConditionalFrameOutputTests, DefaultAllowsEveryFrame) {
    ConditionalFrameOutput output;
    EXPECT_TRUE(output.isFrameInRange(0));
    EXPECT_TRUE(output.isFrameInRange(1234));
}

TEST(ConditionalFrameOutputTests, RejectsEmptyString) {
    ConditionalFrameOutput output;
    EXPECT_FALSE(output.parseConditionalFrameRange(""));
}

TEST(ConditionalFrameOutputTests, ParsesSingleFrames) {
    ConditionalFrameOutput output;
    ASSERT_TRUE(output.parseConditionalFrameRange("2,3,5"));
    EXPECT_TRUE(output.isFrameInRange(2));
    EXPECT_TRUE(output.isFrameInRange(3));
    EXPECT_FALSE(output.isFrameInRange(4));
    EXPECT_TRUE(output.isFrameInRange(5));
}

TEST(ConditionalFrameOutputTests, ParsesFrameRangeWithInterval) {
    // "4-4-2": starting at frame 4, a span of 4 frames (4-7), dumping every other one.
    ConditionalFrameOutput output;
    ASSERT_TRUE(output.parseConditionalFrameRange("4-4-2"));
    EXPECT_FALSE(output.isFrameInRange(3));
    EXPECT_TRUE(output.isFrameInRange(4));
    EXPECT_FALSE(output.isFrameInRange(5));
    EXPECT_TRUE(output.isFrameInRange(6));
    EXPECT_FALSE(output.isFrameInRange(7));
    EXPECT_FALSE(output.isFrameInRange(8));
}

TEST(ConditionalFrameOutputTests, ParsesMultipleRanges) {
    // "3-6,10-2": a span of 6 frames starting at 3 (3-8), plus a span of 2 starting at 10 (10-11).
    ConditionalFrameOutput output;
    ASSERT_TRUE(output.parseConditionalFrameRange("3-6,10-2"));
    for (uint64_t frame = 3; frame <= 8; ++frame) {
        EXPECT_TRUE(output.isFrameInRange(frame)) << "frame " << frame;
    }
    EXPECT_FALSE(output.isFrameInRange(9));
    EXPECT_TRUE(output.isFrameInRange(10));
    EXPECT_TRUE(output.isFrameInRange(11));
    EXPECT_FALSE(output.isFrameInRange(12));
}

// ApiDumpSettings::init only needs a VkInstanceCreateInfo with a VkLayerSettingsCreateInfoEXT
// chain to parse settings from - it never touches a device or the Vulkan loader - so these
// construct one directly rather than going through VulkanInstanceBuilder/vkCreateInstance.
TEST_F(ApiDumpTests, CaptureTriggerBoundaryDefaultsToFrames) {
    VkApplicationInfo app_info{layer_test::GetDefaultApplicationInfo()};
    VkInstanceCreateInfo inst_create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    inst_create_info.pApplicationInfo = &app_info;

    ApiDumpSettings settings;
    settings.init(&inst_create_info, nullptr);
    EXPECT_EQ(settings.captureBoundary(), ApiDumpCaptureBoundary::Frames);
}

TEST_F(ApiDumpTests, CaptureTriggerBoundarySelectsQueueSubmits) {
    const char* boundary_value = "queue_submits";
    const std::vector<VkLayerSettingEXT> settings_values = {
        {kLayerName, "capture_trigger_boundary", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &boundary_value}};
    const VkLayerSettingsCreateInfoEXT layer_settings_create_info{VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT, nullptr,
                                                                    static_cast<uint32_t>(settings_values.size()),
                                                                    settings_values.data()};

    VkApplicationInfo app_info{layer_test::GetDefaultApplicationInfo()};
    VkInstanceCreateInfo inst_create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    inst_create_info.pNext = &layer_settings_create_info;
    inst_create_info.pApplicationInfo = &app_info;

    ApiDumpSettings settings;
    settings.init(&inst_create_info, nullptr);
    EXPECT_EQ(settings.captureBoundary(), ApiDumpCaptureBoundary::QueueSubmits);
}

TEST_F(ApiDumpTests, CaptureProcessNameDefaultsToCaptureAll) {
    VkApplicationInfo app_info{layer_test::GetDefaultApplicationInfo()};
    VkInstanceCreateInfo inst_create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    inst_create_info.pApplicationInfo = &app_info;

    ApiDumpSettings settings;
    settings.init(&inst_create_info, nullptr);
    EXPECT_TRUE(settings.processMatchesCaptureName());
}

TEST_F(ApiDumpTests, CaptureProcessNameMatchesCurrentProcess) {
    const std::string current_process_name = GetCurrentProcessName();
    const char* process_name_value = current_process_name.c_str();
    const std::vector<VkLayerSettingEXT> settings_values = {
        {kLayerName, "capture_process_name", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &process_name_value}};
    const VkLayerSettingsCreateInfoEXT layer_settings_create_info{VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT, nullptr,
                                                                    static_cast<uint32_t>(settings_values.size()),
                                                                    settings_values.data()};

    VkApplicationInfo app_info{layer_test::GetDefaultApplicationInfo()};
    VkInstanceCreateInfo inst_create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    inst_create_info.pNext = &layer_settings_create_info;
    inst_create_info.pApplicationInfo = &app_info;

    ApiDumpSettings settings;
    settings.init(&inst_create_info, nullptr);
    EXPECT_TRUE(settings.processMatchesCaptureName());
}

TEST_F(ApiDumpTests, CaptureProcessNameExcludesOtherProcesses) {
    const char* process_name_value = "definitely_not_this_test_binary";
    const std::vector<VkLayerSettingEXT> settings_values = {
        {kLayerName, "capture_process_name", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &process_name_value}};
    const VkLayerSettingsCreateInfoEXT layer_settings_create_info{VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT, nullptr,
                                                                    static_cast<uint32_t>(settings_values.size()),
                                                                    settings_values.data()};

    VkApplicationInfo app_info{layer_test::GetDefaultApplicationInfo()};
    VkInstanceCreateInfo inst_create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    inst_create_info.pNext = &layer_settings_create_info;
    inst_create_info.pApplicationInfo = &app_info;

    ApiDumpSettings settings;
    settings.init(&inst_create_info, nullptr);
    EXPECT_FALSE(settings.processMatchesCaptureName());
    // A process this excludes must not report any frame as recorded either - see init/isFrameRecorded.
    EXPECT_FALSE(settings.isFrameRecorded(0));
}

// ApiDumpInstance is normally reached only through the process-wide current() singleton, but its
// constructor is public and copy/move are merely deleted (not the default constructor), so these
// construct one directly - avoiding shared mutable state (frame_count, queue_submit_count, ...)
// across tests that current() would otherwise force.
class ApiDumpInstanceTests : public VkTestFramework {
   public:
    ~ApiDumpInstanceTests(){};

    static void SetUpTestSuite() {}
    static void TearDownTestSuite(){};

    // Mirrors the ApiDumpSettings::init() pattern used above, but through an ApiDumpInstance's own
    // contained settings object, so nextFrame/notifyQueueSubmit/checkFrameBoundaryInSubmit have a
    // fully initialized ConditionalFrameOutput/document state to work against.
    static void InitWithSettings(ApiDumpInstance& instance, const std::vector<VkLayerSettingEXT>& settings_values) {
        const VkLayerSettingsCreateInfoEXT layer_settings_create_info{VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT, nullptr,
                                                                        static_cast<uint32_t>(settings_values.size()),
                                                                        settings_values.data()};
        VkApplicationInfo app_info{layer_test::GetDefaultApplicationInfo()};
        VkInstanceCreateInfo inst_create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        inst_create_info.pNext = settings_values.empty() ? nullptr : &layer_settings_create_info;
        inst_create_info.pApplicationInfo = &app_info;
        instance.settings().init(&inst_create_info, nullptr);
    }
};

TEST_F(ApiDumpInstanceTests, FrameCountAndQueueSubmitCountAdvanceIndependently) {
    ApiDumpInstance instance;
    InitWithSettings(instance, {});

    // A compute-only workload: no vkQueuePresentKHR-equivalent call ever happens, only submissions.
    instance.notifyQueueSubmit();
    instance.notifyQueueSubmit();
    instance.notifyQueueSubmit();

    EXPECT_EQ(instance.frameCount(), 0u);
    EXPECT_EQ(instance.queueSubmitCount(), 3u);
}

TEST_F(ApiDumpInstanceTests, NotifyQueueSubmitAlwaysAdvancesRegardlessOfBoundary) {
    // Confirms queue_submit_count is not conditional on capture_trigger_boundary, mirroring how
    // frame_count is not conditional on it either - both counters always tick.
    ApiDumpInstance instance;
    InitWithSettings(instance, {});
    ASSERT_EQ(instance.settings().captureBoundary(), ApiDumpCaptureBoundary::Frames);

    instance.notifyQueueSubmit();

    EXPECT_EQ(instance.queueSubmitCount(), 1u);
}

TEST_F(ApiDumpInstanceTests, FrameBoundaryExtEndBitAdvancesFrameCount) {
    ApiDumpInstance instance;
    InitWithSettings(instance, {});

    VkFrameBoundaryEXT frame_boundary{VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT};
    frame_boundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.pNext = &frame_boundary;

    instance.checkFrameBoundaryInSubmit(1, &submit_info);

    EXPECT_EQ(instance.frameCount(), 1u);
}

TEST_F(ApiDumpInstanceTests, FrameBoundaryExtWithoutEndBitDoesNotAdvanceFrameCount) {
    ApiDumpInstance instance;
    InitWithSettings(instance, {});

    VkFrameBoundaryEXT frame_boundary{VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT};
    frame_boundary.flags = 0;
    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.pNext = &frame_boundary;

    instance.checkFrameBoundaryInSubmit(1, &submit_info);

    EXPECT_EQ(instance.frameCount(), 0u);
}

TEST_F(ApiDumpInstanceTests, FrameBoundaryExtAdvancesFrameCountEvenInQueueSubmitsBoundary) {
    // A frame boundary is a frame boundary independent of what output_range/capture_trigger is
    // currently counting - see ApiDumpInstance::nextFrame.
    ApiDumpInstance instance;
    const char* boundary_value = "queue_submits";
    InitWithSettings(instance,
                      {{kLayerName, "capture_trigger_boundary", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &boundary_value}});
    ASSERT_EQ(instance.settings().captureBoundary(), ApiDumpCaptureBoundary::QueueSubmits);

    VkFrameBoundaryEXT frame_boundary{VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT};
    frame_boundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.pNext = &frame_boundary;

    instance.checkFrameBoundaryInSubmit(1, &submit_info);

    EXPECT_EQ(instance.frameCount(), 1u);
}

TEST_F(ApiDumpInstanceTests, PreRangeQueueSubmitIsForceDumpedAndFlaggedSetup) {
    // A queue submission before output_range_queue_submits opens must still be dumped under
    // always_dump_setup - previously it was silently dropped entirely, since vkQueueSubmit was not
    // classified as a setup command - and flagged isSetupSubmission via currentCommandIsSetup().
    ApiDumpInstance instance;
    const char* boundary_value = "queue_submits";
    const char* range_value = "2-0";  // starts at submission 2 (0-based), unlimited count
    VkBool32 always_dump_setup = VK_TRUE;
    InitWithSettings(instance, {{kLayerName, "capture_trigger_boundary", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &boundary_value},
                                 {kLayerName, "output_range_queue_submits", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &range_value},
                                 {kLayerName, "always_dump_setup", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &always_dump_setup}});

    // Submission 0: still before the range.
    instance.setCurrentCommand("vkQueueSubmit");
    EXPECT_TRUE(instance.shouldDumpOutput()) << "a pre-range submission must still be dumped under always_dump_setup";
    EXPECT_TRUE(instance.currentCommandIsSetup());
    instance.notifyQueueSubmit();

    // Submission 1: still before the range (range starts at submission 2).
    instance.setCurrentCommand("vkQueueSubmit");
    EXPECT_TRUE(instance.currentCommandIsSetup());
    instance.notifyQueueSubmit();

    // Submission 2: now in range.
    instance.setCurrentCommand("vkQueueSubmit");
    EXPECT_TRUE(instance.shouldDumpOutput());
    EXPECT_FALSE(instance.currentCommandIsSetup()) << "in-range content is not force-dumped setup content";
}

TEST_F(ApiDumpInstanceTests, QueueSubmitIsNeverSetupContentInFramesBoundary) {
    // The classification added for isSetupSubmission must not change Frames boundary behavior.
    ApiDumpInstance instance;
    VkBool32 always_dump_setup = VK_TRUE;
    InitWithSettings(instance, {{kLayerName, "always_dump_setup", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &always_dump_setup}});
    ASSERT_EQ(instance.settings().captureBoundary(), ApiDumpCaptureBoundary::Frames);

    instance.setCurrentCommand("vkQueueSubmit");

    EXPECT_FALSE(instance.currentCommandIsSetup());
}

TEST_F(ApiDumpInstanceTests, FrameBoundaryAndroidAdvancesFrameCountWithNoDriverResolved) {
    // The driver-forwarding pointer is resolved separately at CreateDevice time (see
    // api_dump_handwritten_functions.h); this only exercises the interception's own behavior, which
    // must not crash and must still advance the real frame counter when nothing is resolved yet.
    ApiDumpInstance instance;
    InitWithSettings(instance, {});

    instance.frameBoundaryAndroid(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE);

    EXPECT_EQ(instance.frameCount(), 1u);
}

// ---------------------------------------------------------------------------------------------------
// Asynchronous file writes (async_write / buffer_size_kb), and the formatting helpers that were made
// cheaper alongside them. Everything here has to produce exactly what the code it replaced produced.
// ---------------------------------------------------------------------------------------------------

namespace {

std::string ReadWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

// Removes the file when it goes out of scope, so a failing test does not leave it behind.
struct ScopedFile {
    explicit ScopedFile(std::string name) : path(std::move(name)) {}
    ~ScopedFile() { std::remove(path.c_str()); }
    std::string path;
};

}  // namespace

class ApiDumpAsyncFileBufTests : public VkTestFramework {
   public:
    ~ApiDumpAsyncFileBufTests(){};

    static void SetUpTestSuite() {}
    static void TearDownTestSuite(){};
};

TEST_F(ApiDumpAsyncFileBufTests, WritesEverythingInOrderAcrossManyBlocks) {
    ScopedFile file("api_dump_async_buf_contents.tmp");
    // The smallest pool, so every block is 16 KiB and the data below crosses many of them.
    auto buf = ApiDumpAsyncFileBuf::Open(file.path, ApiDumpAsyncFileBuf::kMinBufferBytes);
    ASSERT_NE(buf, nullptr);
    std::ostream out(buf.get());

    std::string expected;
    for (int i = 0; i < 6000; ++i) {
        // Mixes small writes with ones larger than a whole block, and hand-offs part way through blocks.
        const std::string text =
            (i % 50 == 0) ? std::string(40 * 1024, static_cast<char>('a' + i % 26)) : "line " + std::to_string(i) + "\n";
        out << text;
        expected += text;
        if (i % 7 == 0) buf->handOff();
    }
    ASSERT_TRUE(out.good());

    // Drained, so all of it is already in the file, before anything is closed.
    EXPECT_TRUE(buf->drain(std::chrono::milliseconds(30000)));
    EXPECT_EQ(ReadWholeFile(file.path), expected);
    buf->close();
    EXPECT_EQ(ReadWholeFile(file.path), expected);
}

TEST_F(ApiDumpAsyncFileBufTests, DrainMakesDataVisibleMidBlock) {
    ScopedFile file("api_dump_async_buf_drain.tmp");
    auto buf = ApiDumpAsyncFileBuf::Open(file.path, 256 * 1024);
    ASSERT_NE(buf, nullptr);
    std::ostream out(buf.get());

    std::string expected;
    for (int i = 0; i < 20; ++i) {
        const std::string text = "record " + std::to_string(i) + "\n";
        out << text;
        expected += text;
        ASSERT_TRUE(buf->drain(std::chrono::milliseconds(10000)));
        EXPECT_EQ(ReadWholeFile(file.path), expected);
    }
}

TEST_F(ApiDumpAsyncFileBufTests, WritingAfterCloseIsDiscardedWithoutBlocking) {
    ScopedFile file("api_dump_async_buf_closed.tmp");
    auto buf = ApiDumpAsyncFileBuf::Open(file.path, ApiDumpAsyncFileBuf::kMinBufferBytes);
    ASSERT_NE(buf, nullptr);
    std::ostream out(buf.get());
    out << "kept";
    buf->close();
    buf->close();  // idempotent

    // Far more than the whole pool: must neither block nor reach the file.
    out << std::string(8 * 1024 * 1024, 'x');
    out.flush();
    EXPECT_EQ(ReadWholeFile(file.path), "kept");
}

TEST_F(ApiDumpAsyncFileBufTests, FailedWritesAreDiscardedWithoutBlockingTheProducer) {
    ScopedFile file("api_dump_async_buf_failure.tmp");
    {
        std::ofstream create(file.path);
        create << "x";
    }
    // Opened read only, so every write to it fails.
    std::FILE* read_only = std::fopen(file.path.c_str(), "rb");
    ASSERT_NE(read_only, nullptr);
    auto buf = ApiDumpAsyncFileBuf::Adopt(read_only, ApiDumpAsyncFileBuf::kMinBufferBytes);
    ASSERT_NE(buf, nullptr);
    std::ostream out(buf.get());

    const std::string chunk(1024 * 1024, 'y');
    for (int i = 0; i < 16; ++i) {  // 16 MiB through a 64 KiB pool
        out << chunk;
        buf->handOff();
    }
    EXPECT_FALSE(buf->drain(std::chrono::milliseconds(5000)));
    EXPECT_TRUE(buf->failed());
}

TEST_F(ApiDumpAsyncFileBufTests, SerialisedProducersNeverTearALine) {
    ScopedFile file("api_dump_async_buf_threads.tmp");
    auto buf = ApiDumpAsyncFileBuf::Open(file.path, 128 * 1024);
    ASSERT_NE(buf, nullptr);
    std::ostream out(buf.get());
    std::mutex output_mutex;  // plays the part of the layer's output mutex

    constexpr int kThreads = 4;
    constexpr int kLines = 5000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kLines; ++i) {
                std::lock_guard<std::mutex> lock(output_mutex);
                out << "thread " << t << " line " << i << " payload payload payload\n";
                if (i % 97 == 0) buf->handOff();
            }
        });
    }
    for (auto& thread : threads) thread.join();
    buf->close();

    std::istringstream lines(ReadWholeFile(file.path));
    std::string line;
    std::vector<int> next(kThreads, 0);
    int count = 0;
    while (std::getline(lines, line)) {
        int t = -1, i = -1;
        ASSERT_EQ(std::sscanf(line.c_str(), "thread %d line %d payload payload payload", &t, &i), 2) << line;
        ASSERT_TRUE(t >= 0 && t < kThreads);
        EXPECT_EQ(i, next[t]++) << "a thread's own lines must stay in order";
        ++count;
    }
    EXPECT_EQ(count, kThreads * kLines);
}

TEST_F(ApiDumpAsyncFileBufTests, OpenFailsForAnUncreatableFile) {
    EXPECT_EQ(ApiDumpAsyncFileBuf::Open("no_such_directory_for_api_dump_test/out.json", 64 * 1024), nullptr);
}

TEST_F(ApiDumpInstanceTests, AsyncWriteSupersedesFlushAndHandsOffAtFrameBoundaries) {
    ScopedFile file("api_dump_async_instance.json");
    const char* format = "json";
    const char* filename = file.path.c_str();
    VkBool32 async_write = VK_TRUE;
    VkBool32 flush = VK_TRUE;  // on, and async_write has to win
    int32_t buffer_size_kb = 64;
    ApiDumpInstance instance;
    InitWithSettings(instance, {{kLayerName, "output_format", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &format},
                                 {kLayerName, "log_filename", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &filename},
                                 {kLayerName, "async_write", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &async_write},
                                 {kLayerName, "flush", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &flush},
                                 {kLayerName, "buffer_size_kb", VK_LAYER_SETTING_TYPE_INT32_EXT, 1, &buffer_size_kb}});
    EXPECT_FALSE(instance.settings().shouldFlush());

    instance.settings().stream() << "first frame content\n";
    instance.nextFrame();  // a frame boundary hands the data to the writer without waiting for it

    // Not waited for, so poll until the writer has caught up.
    bool seen = false;
    for (int attempt = 0; attempt < 2000 && !seen; ++attempt) {
        seen = ReadWholeFile(file.path).find("first frame content") != std::string::npos;
        if (!seen) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(seen);

    // Anything written since is only in memory until another layer asks for it to be written out.
    instance.settings().stream() << "second frame content\n";
    {
        std::lock_guard<std::mutex> lock(instance.outputMutex());
        EXPECT_TRUE(instance.settings().drainOutput(std::chrono::milliseconds(10000)));
    }
    EXPECT_NE(ReadWholeFile(file.path).find("second frame content"), std::string::npos);
}

TEST_F(ApiDumpInstanceTests, FlushExportDrainsAndTimesOutInsteadOfHanging) {
    ScopedFile file("api_dump_async_export.json");
    const char* format = "json";
    const char* filename = file.path.c_str();
    VkBool32 async_write = VK_TRUE;
    ApiDumpInstance& instance = ApiDumpInstance::current();
    InitWithSettings(instance, {{kLayerName, "output_format", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &format},
                                 {kLayerName, "log_filename", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &filename},
                                 {kLayerName, "async_write", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &async_write}});
    {
        std::lock_guard<std::mutex> lock(instance.outputMutex());
        instance.settings().stream() << "written before the flush call\n";
    }
    EXPECT_EQ(vkFlushAPIDUMP(10000), VK_TRUE);
    EXPECT_NE(ReadWholeFile(file.path).find("written before the flush call"), std::string::npos);

    // Another thread inside a long call, holding the output mutex: the flush gives up rather than hangs.
    std::atomic<bool> holding{false};
    std::atomic<bool> release{false};
    std::thread holder([&] {
        std::lock_guard<std::mutex> lock(instance.outputMutex());
        holding = true;
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    while (!holding) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(vkFlushAPIDUMP(50), VK_FALSE);
    release = true;
    holder.join();
    EXPECT_EQ(vkFlushAPIDUMP(10000), VK_TRUE);  // and works again once the mutex is free
}

TEST_F(ApiDumpInstanceTests, BufferSizeAloneEnlargesTheSynchronousStreamAndDrainFlushesIt) {
    ScopedFile file("api_dump_buffered_instance.json");
    const char* format = "json";
    const char* filename = file.path.c_str();
    VkBool32 flush = VK_FALSE;
    int32_t buffer_size_kb = 1024;
    ApiDumpInstance instance;
    InitWithSettings(instance, {{kLayerName, "output_format", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &format},
                                 {kLayerName, "log_filename", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &filename},
                                 {kLayerName, "flush", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &flush},
                                 {kLayerName, "buffer_size_kb", VK_LAYER_SETTING_TYPE_INT32_EXT, 1, &buffer_size_kb}});
    instance.settings().stream() << "buffered\n";
    EXPECT_EQ(ReadWholeFile(file.path).find("buffered"), std::string::npos) << "still held in the 1 MiB buffer";

    // A frame boundary flushes the stream, so a kill loses at most one frame even with flush off.
    instance.nextFrame();
    EXPECT_NE(ReadWholeFile(file.path).find("buffered"), std::string::npos);

    // And so does another layer asking for everything to be written out.
    instance.settings().stream() << "after the boundary\n";
    {
        std::lock_guard<std::mutex> lock(instance.outputMutex());
        EXPECT_TRUE(instance.settings().drainOutput(std::chrono::milliseconds(1000)));
    }
    EXPECT_NE(ReadWholeFile(file.path).find("after the boundary"), std::string::npos);
}

TEST_F(ApiDumpInstanceTests, FrameBoundaryDoesNotFlushWhenFlushIsOn) {
    // With the layer's default flush on every call has already been flushed, so a boundary adds nothing.
    ScopedFile file("api_dump_flush_on_instance.json");
    const char* format = "json";
    const char* filename = file.path.c_str();
    int32_t buffer_size_kb = 1024;  // large enough that only a flush could make the data visible
    ApiDumpInstance instance;
    InitWithSettings(instance, {{kLayerName, "output_format", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &format},
                                 {kLayerName, "log_filename", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &filename},
                                 {kLayerName, "buffer_size_kb", VK_LAYER_SETTING_TYPE_INT32_EXT, 1, &buffer_size_kb}});
    ASSERT_TRUE(instance.settings().shouldFlush());
    instance.settings().stream() << "unflushed\n";
    instance.nextFrame();
    EXPECT_EQ(ReadWholeFile(file.path).find("unflushed"), std::string::npos);
}

TEST_F(ApiDumpInstanceTests, FlushStaysOnByDefaultWithoutAsyncWrite) {
    // The default behaviour is unchanged: a flush after every call.
    ScopedFile file("api_dump_default_instance.json");
    const char* format = "json";
    const char* filename = file.path.c_str();
    ApiDumpInstance instance;
    InitWithSettings(instance, {{kLayerName, "output_format", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &format},
                                 {kLayerName, "log_filename", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &filename}});
    EXPECT_TRUE(instance.settings().shouldFlush());
}

TEST_F(ApiDumpInstanceTests, IndentationMatchesTheSetwPaddingItReplaced) {
    const char* format = "json";
    for (const bool use_spaces : {true, false}) {
        for (const int indent_size : {0, 1, 2, 4, 8}) {
            ApiDumpInstance instance;
            VkBool32 spaces = use_spaces ? VK_TRUE : VK_FALSE;
            int32_t size = indent_size;
            InitWithSettings(instance, {{kLayerName, "output_format", VK_LAYER_SETTING_TYPE_STRING_EXT, 1, &format},
                                         {kLayerName, "use_spaces", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &spaces},
                                         {kLayerName, "indent_size", VK_LAYER_SETTING_TYPE_INT32_EXT, 1, &size}});
            // Out of order and growing, as real calls are. A tab indent is one tab per level.
            for (const int indents : {0, 3, 1, 12, 2, 40, 1, 0, 7}) {
                std::ostringstream expected;
                expected << std::setfill(use_spaces ? ' ' : '\t') << std::setw(indents * (use_spaces ? indent_size : 1)) << "";
                std::ostringstream actual;
                actual << instance.settings().indentation(indents);
                EXPECT_EQ(actual.str(), expected.str())
                    << "indents " << indents << " size " << indent_size << " spaces " << use_spaces;
            }
        }
    }
}

template <ApiDumpFormat Format>
static std::string StringstreamElementName(const char* name, size_t i) {
    std::stringstream stream;
    if constexpr (Format == ApiDumpFormat::Text || Format == ApiDumpFormat::Html) stream << name;
    stream << "[" << i << "]";
    return stream.str();
}

template <ApiDumpFormat Format>
static std::string StringstreamElementName(const char* name, size_t i, size_t j) {
    std::stringstream stream;
    if constexpr (Format == ApiDumpFormat::Text || Format == ApiDumpFormat::Html) stream << name;
    stream << "[" << i << "][" << j << "]";
    return stream.str();
}

template <ApiDumpFormat Format>
static void ExpectElementNamesMatchTheStringstreamTheyReplaced() {
    const std::string too_long_for_the_stack_buffer(400, 'n');
    for (const char* name : {"pBufferMemoryBarriers", "x", "", too_long_for_the_stack_buffer.c_str()}) {
        for (const size_t i : {size_t{0}, size_t{7}, size_t{123456789}, ~size_t{0}}) {
            EXPECT_EQ(std::string(ArrayElementName<Format>(name, i).c_str()), StringstreamElementName<Format>(name, i));
            for (const size_t j : {size_t{0}, size_t{3}, size_t{99999}}) {
                EXPECT_EQ(std::string(ArrayElementName<Format>(name, i, j).c_str()), StringstreamElementName<Format>(name, i, j));
            }
        }
    }
}

TEST_F(ApiDumpInstanceTests, ArrayElementNamesMatchTheStringstreamTheyReplaced) {
    ExpectElementNamesMatchTheStringstreamTheyReplaced<ApiDumpFormat::Text>();
    ExpectElementNamesMatchTheStringstreamTheyReplaced<ApiDumpFormat::Html>();
    ExpectElementNamesMatchTheStringstreamTheyReplaced<ApiDumpFormat::Json>();
}

TEST_F(ApiDumpInstanceTests, ThreadIdsAreNumberedPerInstanceAndStayStable) {
    ApiDumpInstance a;
    EXPECT_EQ(a.threadID(), 0u);
    EXPECT_EQ(a.threadID(), 0u);  // second call is served from the per-thread cache
    uint64_t other_thread_id = 99;
    std::thread([&] {
        other_thread_id = a.threadID();
        EXPECT_EQ(a.threadID(), other_thread_id);
    }).join();
    EXPECT_EQ(other_thread_id, 1u);
    EXPECT_EQ(a.threadID(), 0u);

    // A second instance numbers its own threads from zero, not from the first instance's cached value.
    ApiDumpInstance b;
    uint64_t b_other_thread_id = 99;
    std::thread([&] { b_other_thread_id = b.threadID(); }).join();
    EXPECT_EQ(b_other_thread_id, 0u);
    EXPECT_EQ(b.threadID(), 1u);
}
