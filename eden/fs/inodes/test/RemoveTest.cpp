/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "eden/fs/inodes/FileData.h"
#include "eden/fs/inodes/FileInode.h"
#include "eden/fs/inodes/TreeInode.h"
#include "eden/fs/testharness/FakeTreeBuilder.h"
#include "eden/fs/testharness/TestChecks.h"
#include "eden/fs/testharness/TestMount.h"
#include "eden/utils/test/TestChecks.h"

using namespace facebook::eden;
using folly::StringPiece;

class UnlinkTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Set up a directory structure that we will use for most
    // of the tests below
    FakeTreeBuilder builder;
    builder.setFiles({
        {"dir/a.txt", "This is a.txt.\n"},
        {"dir/b.txt", "This is b.txt.\n"},
        {"dir/c.txt", "This is c.txt.\n"},
        {"readme.txt", "File in the root directory.\n"},
    });
    mount_.initialize(builder);
  }

  TestMount mount_;
};

TEST_F(UnlinkTest, enoent) {
  auto dir = mount_.getTreeInode("dir");
  auto unlinkFuture = dir->unlink(PathComponentPiece{"notpresent.txt"});
  ASSERT_TRUE(unlinkFuture.isReady());
  EXPECT_THROW_ERRNO(unlinkFuture.get(), ENOENT);
}

TEST_F(UnlinkTest, notLoaded) {
  auto dir = mount_.getTreeInode("dir");
  auto childPath = PathComponentPiece{"a.txt"};

  // Remove the child when it has not been loaded yet.
  auto unlinkFuture = dir->unlink(childPath);
  ASSERT_TRUE(unlinkFuture.isReady());
  unlinkFuture.get();

  EXPECT_THROW_ERRNO(dir->getChildInodeNumber(childPath), ENOENT);
}

TEST_F(UnlinkTest, inodeAssigned) {
  auto dir = mount_.getTreeInode("dir");
  auto childPath = PathComponentPiece{"a.txt"};

  // Assign an inode number to the child without loading it.
  dir->getChildInodeNumber(childPath);
  auto unlinkFuture = dir->unlink(childPath);
  ASSERT_TRUE(unlinkFuture.isReady());
  unlinkFuture.get();

  EXPECT_THROW_ERRNO(dir->getChildInodeNumber(childPath), ENOENT);
}

TEST_F(UnlinkTest, loaded) {
  auto dir = mount_.getTreeInode("dir");
  auto childPath = PathComponentPiece{"a.txt"};

  // Load the child before removing it
  auto file = mount_.getFileInode("dir/a.txt");
  EXPECT_EQ(file->getNodeId(), dir->getChildInodeNumber(childPath));
  auto unlinkFuture = dir->unlink(childPath);
  ASSERT_TRUE(unlinkFuture.isReady());
  unlinkFuture.get();

  EXPECT_THROW_ERRNO(dir->getChildInodeNumber(childPath), ENOENT);
  // We should still be able to read from the FileInode
  EXPECT_FILE_INODE(file, "This is a.txt.\n", 0644);
}

TEST_F(UnlinkTest, modified) {
  auto dir = mount_.getTreeInode("dir");
  auto childPath = PathComponentPiece{"a.txt"};

  // Modify the child, so it is materialized before we remove it
  auto file = mount_.getFileInode("dir/a.txt");
  EXPECT_EQ(file->getNodeId(), dir->getChildInodeNumber(childPath));
  auto fileData = file->getOrLoadData();
  fileData->materializeForWrite(O_WRONLY).get();
  auto newContents = StringPiece{
      "new contents for the file\n"
      "testing testing\n"
      "123\n"
      "testing testing\n"};
  auto writeResult = fileData->write(newContents, 0);
  EXPECT_EQ(newContents.size(), writeResult);

  // Now remove the child
  auto unlinkFuture = dir->unlink(childPath);
  ASSERT_TRUE(unlinkFuture.isReady());
  unlinkFuture.get();

  EXPECT_THROW_ERRNO(dir->getChildInodeNumber(childPath), ENOENT);
  // We should still be able to read from the FileInode
  EXPECT_FILE_INODE(file, newContents, 0644);
}

TEST_F(UnlinkTest, created) {
  auto dir = mount_.getTreeInode("dir");
  auto childPath = PathComponentPiece{"new.txt"};
  auto contents =
      StringPiece{"This is a new file that does not exist in source control\n"};
  mount_.addFile("dir/new.txt", contents);
  auto file = mount_.getFileInode("dir/new.txt");

  // Now remove the child
  auto unlinkFuture = dir->unlink(childPath);
  ASSERT_TRUE(unlinkFuture.isReady());
  unlinkFuture.get();

  EXPECT_THROW_ERRNO(dir->getChildInodeNumber(childPath), ENOENT);
  // We should still be able to read from the FileInode
  EXPECT_FILE_INODE(file, contents, 0644);
}

// TODO: It would be nice to adds some tests for concurrent load+unlink
// However, loading a FileInode does not wait for the file data to be loaded
// from the ObjectStore, so we currently don't have a good way to test
// various interleavings of the two operations.

// TODO
// - concurrent rename+unlink.  We can block the rename on the destination
//   directory load.  This doesn't really test all corner cases, but is better
//   than nothing.

// TODO rmdir tests:
//
// not empty
//
// not present
// not materialized, completely unloaded
// not materialized, inode assigned
// not materialized, loaded
// materialized, does not exist in source control
// materialized, modified from source control
//
// async:
// - concurrent load+rmdir
// - concurrent rename+rmdir
// - concurrent rmdir+rmdir
//
// - concurrent rename+rmdir+rmdir:
//   1. make sure a/b/c/ is not ready yet.
//   2. start rename(a/b/c --> other_dir/c)
//   3. start rmdir(a/b/c)
//   4. start rmdir(a/b/c)
//   5. make a/b/c ready
//
// - concurrent rename+rmdir+rmdir:
//   1. make sure neither a/b nor a/b/c/ are ready yet.
//   2. start rename(a/b/c --> other_dir/c).then(rmdir a/b)
//   3. start rmdir(a/b/c)
//   4. make a/b/c ready
//   This should hopefully trigger the rmdir(a/b) to succeed before
//   rmdir(a/b/c) completes.
//
// - attempt to create child in subdir after rmdir
// - attempt to mkdir child in subdir after rmdir
