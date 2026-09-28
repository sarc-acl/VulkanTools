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

#include <cstdarg>
#include <cstdio>
#include <cstring>

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
