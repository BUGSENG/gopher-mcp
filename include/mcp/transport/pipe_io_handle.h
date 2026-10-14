// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Platform-specific includes
#ifdef _WIN32
#include <io.h>
#include <winsock2.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "mcp/network/io_socket_handle_impl.h"

namespace mcp {
namespace transport {

/**
 * PipeIoHandle - Simplified IoHandle for pipes with separate read/write FDs
 *
 * This class extends IoSocketHandleImpl and overrides just the methods needed
 * to handle separate read and write file descriptors for pipes.
 */
class PipeIoHandle : public network::IoSocketHandleImpl {
 public:
  PipeIoHandle(int read_fd, int write_fd)
      : IoSocketHandleImpl(read_fd),  // Use read_fd as the primary FD
        write_fd_(write_fd) {}

  ~PipeIoHandle() override {
    // Close write FD if still open
    if (write_fd_ >= 0) {
#ifdef _WIN32
      _close(write_fd_);  // CRT close for pipe fds created with _pipe()
#else
      ::close(write_fd_);
#endif
      write_fd_ = -1;
    }
  }

  // A pipe's read end can never tell us that its separate write end has
  // space again. Keep both events under one caller-owned FileEvent so normal
  // connection activation, disabling and deferred destruction cover both.
  event::FileEventPtr createFileEvent(event::Dispatcher& dispatcher,
                                      event::FileReadyCb cb,
                                      event::FileTriggerType trigger,
                                      uint32_t events) override {
    class PipeFileEvent : public event::FileEvent {
     public:
      PipeFileEvent(event::Dispatcher& dispatcher,
                    int read_fd,
                    int write_fd,
                    event::FileReadyCb cb,
                    event::FileTriggerType trigger,
                    uint32_t events)
          : read_event_(dispatcher.createFileEvent(
                read_fd, cb, trigger, readEvents(events))),
            write_event_(dispatcher.createFileEvent(
                write_fd, std::move(cb), trigger, writeEvents(events))) {}

      void activate(uint32_t events) override {
        if (readEvents(events))
          read_event_->activate(readEvents(events));
        if (writeEvents(events))
          write_event_->activate(writeEvents(events));
      }
      void setEnabled(uint32_t events) override {
        read_event_->setEnabled(readEvents(events));
        write_event_->setEnabled(writeEvents(events));
      }
      void unregisterEventIfEmulatedEdge(uint32_t events) override {
        if (readEvents(events))
          read_event_->unregisterEventIfEmulatedEdge(readEvents(events));
        if (writeEvents(events))
          write_event_->unregisterEventIfEmulatedEdge(writeEvents(events));
      }
      void registerEventIfEmulatedEdge(uint32_t events) override {
        if (readEvents(events))
          read_event_->registerEventIfEmulatedEdge(readEvents(events));
        if (writeEvents(events))
          write_event_->registerEventIfEmulatedEdge(writeEvents(events));
      }

     private:
      static uint32_t readEvents(uint32_t events) {
        return events & ~static_cast<uint32_t>(event::FileReadyType::Write);
      }
      static uint32_t writeEvents(uint32_t events) {
        return events & static_cast<uint32_t>(event::FileReadyType::Write);
      }
      event::FileEventPtr read_event_;
      event::FileEventPtr write_event_;
    };
    return std::make_unique<PipeFileEvent>(dispatcher, fd(), write_fd_,
                                           std::move(cb), trigger, events);
  }

  // Override writev to use the write FD instead of the read FD
  network::IoCallResult writev(const ConstRawSlice* slices,
                               size_t num_slices) override {
    if (write_fd_ < 0) {
      return network::IoCallResult::error(EBADF);
    }

    if (num_slices == 0) {
      return network::IoCallResult::success(0);
    }

    // Writing to pipes: handle one slice at a time for simplicity
    size_t total_written = 0;
    for (size_t i = 0; i < num_slices; ++i) {
#ifdef _WIN32
      // Use CRT _write for pipes created with _pipe()
      int result = _write(write_fd_, slices[i].mem_,
                          static_cast<unsigned int>(slices[i].len_));
#else
      ssize_t result = ::write(write_fd_, slices[i].mem_, slices[i].len_);
#endif
      if (result >= 0) {
        total_written += result;
        if (static_cast<size_t>(result) < slices[i].len_) {
          // Partial write, stop here
          break;
        }
      } else {
        // CRT pipes use errno on all platforms
        int err = errno;
        if (total_written > 0) {
          // Return what we've written so far
          return network::IoCallResult::success(total_written);
        }
        if (err == EAGAIN || err == EWOULDBLOCK) {
          // Would block - this is normal for non-blocking pipes
          return network::IoCallResult::error(EAGAIN);
        }
        return network::IoCallResult::error(err);
      }
    }
    return network::IoCallResult::success(total_written);
  }

  // Override close to close both FDs
  network::IoVoidResult close() override {
    // Close write FD first
    if (write_fd_ >= 0) {
#ifdef _WIN32
      _close(write_fd_);  // CRT close for pipe fds created with _pipe()
#else
      ::close(write_fd_);
#endif
      write_fd_ = -1;
    }
    // Then close read FD via parent class
    return IoSocketHandleImpl::close();
  }

 private:
  int write_fd_;  // Separate FD for writing
};

}  // namespace transport
}  // namespace mcp