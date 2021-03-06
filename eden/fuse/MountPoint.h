/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include "eden/utils/PathFuncs.h"

struct stat;

namespace facebook {
namespace eden {
namespace fusell {

class Dispatcher;
class Channel;

class MountPoint {
 public:
  explicit MountPoint(AbsolutePathPiece path, Dispatcher* dispatcher);
  virtual ~MountPoint();

  const AbsolutePath& getPath() const {
    return path_;
  }

  /**
   * Spawn a new thread to mount the filesystem and run the fuse channel.
   *
   * This is similar to run(), except that it returns as soon as the filesystem
   * has been successfully mounted.
   *
   * If an onStop() argument is supplied, this will be called from the FUSE
   * channel thread after the mount point is stopped, just before the thread
   * terminates.  (This happens once the mount point is unmounted.)
   *
   * If start() throws an exception, onStop() will not be called.
   */
  void start(bool debug);
  void start(bool debug, const std::function<void()>& onStop);

  /**
   * Mount the file system, and run the fuse channel.
   *
   * This function will not return until the filesystem is unmounted.
   */
  void run(bool debug);

  uid_t getUid() const {
    return uid_;
  }

  gid_t getGid() const {
    return gid_;
  }

  /**
   * Returns the channel associated with this mount point.
   *
   * No smart pointer because the lifetime is managed solely
   * by the MountPoint instance.
   *
   * This method does not perform any synchronization.  It is the caller's
   * responsibility to synchronize any calls to getChannel() with the call to
   * start().  It is safe to access before start() has been called and once
   * start() returns.
   */
  Channel* getChannel() {
    return channel_.get();
  }

  /**
   * Indicate that the mount point has been successfully started.
   *
   * This function should only be invoked by the Dispatcher class.
   */
  void mountStarted();

  /**
   * Return a stat structure that has been minimally initialized with
   * data for this mount point.
   *
   * The caller must still initialize all file-specific data (inode number,
   * file mode, size, timestamps, link count, etc).
   */
  struct stat initStatData() const;

 private:
  enum class Status { UNINIT, STARTING, RUNNING, ERROR };

  // Forbidden copy constructor and assignment operator
  MountPoint(MountPoint const&) = delete;
  MountPoint& operator=(MountPoint const&) = delete;

  AbsolutePath const path_; // the path where this MountPoint is mounted
  uid_t uid_;
  gid_t gid_;

  Dispatcher* const dispatcher_;
  std::unique_ptr<Channel> channel_;

  std::mutex mutex_;
  std::condition_variable statusCV_;
  Status status_{Status::UNINIT};
  std::exception_ptr startError_;
};
}
}
} // facebook::eden::fusell
