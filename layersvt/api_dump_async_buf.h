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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#include <new>

#if defined(__ANDROID__)
#include <android/log.h>
#endif
#if !defined(_WIN32)
#include <pthread.h>
#endif

// A std::streambuf that moves the writing of the dump file off the thread that produces it.
//
// The dump is formatted straight into a block of memory by the ordinary ostream insertion operators,
// so nothing is copied on the producer's side. A block that fills up, or that is handed off early with
// handOff(), is queued for a single background thread that writes it to the file. The producer only
// ever waits when every block is still queued or being written, which keeps memory use fixed instead
// of growing with the backlog: it is a bounded pool, not an unbounded queue.
//
// Threading: the producer-side members (the streambuf overrides, handOff, drain, close) must be called
// by one thread at a time. api_dump already guarantees this - every entry point formats under
// ApiDumpInstance's output mutex - so the buffer takes no lock of its own on the fast path. Only the
// queue shared with the writer thread is locked.
//
// Memory use is exactly the pool, `buffer_bytes` split into kBlockCount blocks. The blocks are
// allocated without being written, so the operating system only backs the pages that are actually
// used. The pool hands back the most recently released block first, so when the writer keeps up only
// the first two blocks or so are ever touched.
//
// A block can be queued before it is full: handOff() queues just the bytes written since the last
// hand-off and the producer keeps filling the same block after them. Frame boundaries use this so a
// short frame does not tie up a whole block.
//
// Failure: if a write fails (for example the disk is full) the buffer stops writing, reports it once,
// and from then on silently discards output, so the profiled application is never blocked or crashed
// because the dump could not be written. failed() reports it.
class ApiDumpAsyncFileBuf final : public std::streambuf {
   public:
    // The pool is split into this many equal blocks.
    static constexpr size_t kBlockCount = 4;
    // Pool sizes outside this range are clamped to it.
    static constexpr size_t kMinBufferBytes = 64 * 1024;
    static constexpr size_t kMaxBufferBytes = 64 * 1024 * 1024;

    // Opens (and truncates) `filename` and starts the writer thread. Returns null if the file cannot
    // be opened or the thread cannot be started; the caller is expected to fall back to a synchronous
    // sink. Never throws.
    static std::unique_ptr<ApiDumpAsyncFileBuf> Open(const std::string &filename, size_t buffer_bytes) {
        std::FILE *file = std::fopen(filename.c_str(), "wb");
        if (file == nullptr) {
            return nullptr;
        }
        return Adopt(file, buffer_bytes);
    }

    // As Open, for a file that is already open for writing. Takes ownership of `file`, closing it
    // whether or not this succeeds. Exists so the failure handling can be tested with a file that
    // refuses writes.
    static std::unique_ptr<ApiDumpAsyncFileBuf> Adopt(std::FILE *file, size_t buffer_bytes) {
        // Parenthesised so a windows.h that defines min and max as macros cannot break it.
        buffer_bytes = (std::min)((std::max)(buffer_bytes, kMinBufferBytes), kMaxBufferBytes);

        // Nothing here throws: this is built into environments that compile with exceptions disabled
        // (Chromium's build, for one), so failure is reported by return value throughout. Allocation
        // uses nothrow new, and the writer thread is started with a call that reports failure by
        // return code - see startWriter.
        std::unique_ptr<ApiDumpAsyncFileBuf> buf(new (std::nothrow) ApiDumpAsyncFileBuf(file, buffer_bytes / kBlockCount));
        if (!buf) {
            std::fclose(file);
            return nullptr;
        }
        // The destructor closes the file from here on, so every failure below just returns null.
        if (!buf->blocksAllocated_ || !buf->startWriter()) {
            return nullptr;
        }
        return buf;
    }

    ~ApiDumpAsyncFileBuf() override { close(); }

    ApiDumpAsyncFileBuf(const ApiDumpAsyncFileBuf &) = delete;
    ApiDumpAsyncFileBuf &operator=(const ApiDumpAsyncFileBuf &) = delete;

    // Queues everything written since the last hand-off for the writer thread and returns without
    // waiting for it to be written. Cheap when there is nothing new, which is the common case at a
    // boundary that was not dumped.
    void handOff() {
        if (closed_) {
            return;
        }
        const size_t end = static_cast<size_t>(pptr() - pbase());
        if (end == handed_off_) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(Segment{active_, handed_off_, end - handed_off_, false});
            ++pushed_;
        }
        work_cv_.notify_one();
        handed_off_ = end;
    }

    // Queues everything written so far and waits until the writer has handed all of it to the
    // operating system, for up to `timeout`. Returns true if it all arrived and no write has failed.
    // The data is then safe from the process being killed: it is in the kernel's page cache, which
    // outlives the process.
    bool drain(std::chrono::milliseconds timeout) {
        handOff();
        std::unique_lock<std::mutex> lock(mutex_);
        const bool arrived = progress_cv_.wait_for(lock, timeout, [this] { return done_ == pushed_; });
        return arrived && !failed();
    }

    // Writes out everything that is left, stops the writer thread and closes the file. After this the
    // buffer silently discards anything else written to it. Safe to call more than once.
    void close() {
        if (closed_) {
            return;
        }
        handOff();
        closed_ = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        work_cv_.notify_all();
        joinWriter();
        if (file_ != nullptr) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    // Whether a write has failed, after which output is being discarded.
    bool failed() const { return failed_.load(); }

    // Size of one block; the pool is kBlockCount of them.
    size_t blockBytes() const { return block_bytes_; }

   protected:
    // A flush request. The data is handed to the writer without waiting for it - waiting is what
    // drain() is for - so a caller that flushes after every call costs a queue push, not a write.
    int sync() override {
        handOff();
        return 0;
    }

    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) {
            handOff();
            return traits_type::not_eof(ch);
        }
        rotate();
        *pptr() = traits_type::to_char_type(ch);
        pbump(1);
        return ch;
    }

    std::streamsize xsputn(const char *s, std::streamsize count) override {
        std::streamsize written = 0;
        while (count > 0) {
            std::streamsize space = epptr() - pptr();
            if (space == 0) {
                rotate();
                continue;
            }
            const std::streamsize chunk = (std::min)(space, count);
            std::memcpy(pptr(), s, static_cast<size_t>(chunk));
            pbump(static_cast<int>(chunk));
            s += chunk;
            count -= chunk;
            written += chunk;
        }
        return written;
    }

   private:
    struct Block {
        std::unique_ptr<char[]> data;
    };

    // A range of one block for the writer to write. `release` marks the last range of a block: the
    // producer is done with it, so once it is written the block can be reused.
    struct Segment {
        Block *block = nullptr;
        size_t offset = 0;
        size_t length = 0;
        bool release = false;
    };

    ApiDumpAsyncFileBuf(std::FILE *file, size_t block_bytes) : file_(file), block_bytes_(block_bytes) {
        // Unbuffered: the blocks are already large, so a second layer of stdio buffering would only copy.
        std::setvbuf(file_, nullptr, _IONBF, 0);

        blocks_.resize(kBlockCount);
        for (Block &block : blocks_) {
            // Not value-initialised, so pages are only backed once they are written.
            block.data.reset(new (std::nothrow) char[block_bytes_]);
            if (!block.data) {
                return;  // blocksAllocated_ stays false, which Adopt reports as failure
            }
        }
        active_ = &blocks_[0];
        for (size_t i = kBlockCount - 1; i >= 1; --i) {
            free_.push_back(&blocks_[i]);
        }
        setp(active_->data.get(), active_->data.get() + block_bytes_);
        blocksAllocated_ = true;
    }

    // Starts the writer thread. Returns false if it could not be started.
    //
    // Off Windows this is pthread_create, which reports failure by return code. std::thread reports it
    // by throwing, which a build without exceptions turns into a terminate - and the whole point of the
    // fallback to a synchronous sink is that failing to start a thread must not take the process down.
    // On Windows std::thread is used, with the failure caught only where exceptions exist to catch it.
    bool startWriter() {
#if defined(_WIN32)
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        try {
            writer_ = std::thread([this] { writerMain(); });
        } catch (...) {
            return false;
        }
#else
        writer_ = std::thread([this] { writerMain(); });
#endif
        writer_started_ = true;
#else
        writer_started_ = pthread_create(
                              &writer_, nullptr,
                              [](void *self) -> void * {
                                  static_cast<ApiDumpAsyncFileBuf *>(self)->writerMain();
                                  return nullptr;
                              },
                              this) == 0;
#endif
        return writer_started_;
    }

    void joinWriter() {
        if (!writer_started_) {
            return;
        }
#if defined(_WIN32)
        writer_.join();
#else
        pthread_join(writer_, nullptr);
#endif
        writer_started_ = false;
    }

    // The active block is full: queue what is left of it, release it, and switch to a free block,
    // waiting for the writer if there is none. This is the only place the producer can wait.
    void rotate() {
        if (closed_ || failed_.load()) {
            // Nobody is going to write this, so keep reusing the same block instead of queueing it.
            handed_off_ = 0;
            setp(active_->data.get(), active_->data.get() + block_bytes_);
            return;
        }

        const size_t end = static_cast<size_t>(pptr() - pbase());
        Block *next = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            pending_.push_back(Segment{active_, handed_off_, end - handed_off_, true});
            ++pushed_;
            work_cv_.notify_one();
            progress_cv_.wait(lock, [this] { return !free_.empty(); });
            next = free_.back();
            free_.pop_back();
        }
        active_ = next;
        handed_off_ = 0;
        setp(active_->data.get(), active_->data.get() + block_bytes_);
    }

    void writerMain() {
#if defined(__linux__) || defined(__ANDROID__)
        pthread_setname_np(pthread_self(), "apidump_write");
#endif
        for (;;) {
            Segment segment;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                work_cv_.wait(lock, [this] { return !pending_.empty() || stop_; });
                if (pending_.empty()) {
                    return;  // stopping, and everything queued has been written
                }
                segment = pending_.front();
                pending_.pop_front();
            }

            if (segment.length > 0 && !failed_.load()) {
                const char *data = segment.block->data.get() + segment.offset;
                if (std::fwrite(data, 1, segment.length, file_) != segment.length) {
                    if (!failed_.exchange(true)) {
                        report("api_dump: writing the output file failed, the rest of the dump is being discarded");
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (segment.release) {
                    free_.push_back(segment.block);
                }
                ++done_;
            }
            progress_cv_.notify_all();
        }
    }

    static void report(const char *message) {
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_ERROR, "api_dump", "%s", message);
#else
        std::fprintf(stderr, "%s\n", message);
#endif
    }

    std::FILE *file_;
    const size_t block_bytes_;
    std::vector<Block> blocks_;

    // Producer side only. The block being filled, and how much of it has already been queued.
    Block *active_ = nullptr;
    size_t handed_off_ = 0;
    bool closed_ = false;

    // Shared with the writer thread.
    std::mutex mutex_;
    std::condition_variable work_cv_;      // a segment was queued, or stop was requested
    std::condition_variable progress_cv_;  // a segment finished, or a block became free
    std::deque<Segment> pending_;
    std::vector<Block *> free_;
    uint64_t pushed_ = 0;
    uint64_t done_ = 0;
    bool stop_ = false;
    std::atomic<bool> failed_{false};

    bool blocksAllocated_ = false;
    bool writer_started_ = false;
#if defined(_WIN32)
    std::thread writer_;
#else
    pthread_t writer_{};
#endif
};
