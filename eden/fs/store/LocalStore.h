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

#include <folly/Range.h>
#include <folly/Synchronized.h>
#include <folly/experimental/StringKeyedUnorderedSet.h>
#include <memory>
#include "eden/fs/store/BlobMetadata.h"
#include "eden/utils/PathFuncs.h"

namespace folly {
template <typename T>
class Optional;
}
namespace rocksdb {
class DB;
class WriteBatch;
}

namespace facebook {
namespace eden {

class Blob;
class Hash;
class StoreResult;
class Tree;

/*
 * LocalStore stores objects (trees and blobs) locally on disk.
 *
 * This is a content-addressed store, so objects can be only retrieved using
 * their hash.
 *
 * The LocalStore is only a cache.  If an object is not found in the LocalStore
 * then it will need to be retrieved from the BackingStore.
 *
 * LocalStore uses RocksDB for the underlying storage.
 *
 * LocalStore is thread-safe, and can be used from multiple threads without
 * requiring the caller to perform locking around accesses to the LocalStore.
 */
class LocalStore {
 public:
  explicit LocalStore(AbsolutePathPiece pathToRocksDb);
  virtual ~LocalStore();

  /**
   * Get arbitrary unserialized data from the store.
   *
   * StoreResult::isValid() will be true if the key was found, and false
   * if the key was not present.
   *
   * May throw exceptions on error.
   */
  StoreResult get(folly::ByteRange key) const;
  StoreResult get(const Hash& id) const;

  /**
   * Get a Tree from the store.
   *
   * Returns nullptr if this key is not present in the store.
   * May throw exceptions on error (e.g., if this ID refers to a non-tree
   * object).
   */
  std::unique_ptr<Tree> getTree(const Hash& id) const;

  /**
   * Get a Blob from the store.
   *
   * Blob objects store file data.
   *
   * Returns nullptr if this key is not present in the store.
   * May throw exceptions on error (e.g., if this ID refers to a non-blob
   * object).
   */
  std::unique_ptr<Blob> getBlob(const Hash& id) const;

  /**
   * Get the size of a blob and the SHA-1 hash of its contents.
   *
   * Returns folly::none if this key is not present in the store, or throws an
   * exception on error.
   */
  folly::Optional<BlobMetadata> getBlobMetadata(const Hash& id) const;

  /**
   * Get the SHA-1 hash of the blob contents for the specified blob.
   *
   * Returns folly::none if this key is not present in the store, or throws an
   * exception on error.
   */
  folly::Optional<Hash> getSha1ForBlob(const Hash& id) const;

  /**
   * Compute the serialized version of the tree.
   * Returns the key and the (not coalesced) serialized data.
   * This does not modify the contents of the store; it is the method
   * used by the putTree method to compute the data that it stores.
   * This is useful when computing the overal set of data during a
   * two phase import. */
  std::pair<Hash, folly::IOBuf> serializeTree(const Tree* tree) const;

  Hash putTree(const Tree* tree);

  /**
   * Store a Blob.
   *
   * Returns a BlobMetadata about the blob, which includes the SHA-1 hash of
   * its contents.
   */
  BlobMetadata putBlob(const Hash& id, const Blob* blob);

  /**
   * Put arbitrary data in the store.
   */
  void put(folly::ByteRange key, folly::ByteRange value);
  void put(const Hash& id, folly::ByteRange value);

  /**
   * Enables batch loading mode.
   * This configures the store to optimize for a bulk load of data
   * during manifest import.
   * In bulk loading mode, put operations will be deferred until flush
   * is called, or until a read operation is performed.
   *
   * The bufferSize configures the maximum amount of data to accumulate
   * (in encoded bytes) before flushing to storage.
   */
  void enableBatchMode(size_t bufferSize);

  /**
   * Disables batch loading mode.
   * This will disable batch loading mode and flush any pending data.
   * This may throw a RocksException if the flush fails.
   */
  void disableBatchMode();

  /**
   * Flushes any batched writes.
   * Will throw RocksException if an error is encountered.
   */
  void flush();

  /**
   * Test whether the key is stored, or whether the key is pending storage
   * as part of batch mode */
  bool hasKey(folly::ByteRange key) const;
  bool hasKey(const Hash& id) const;

 private:
  /**
   * In order to preserve read after write consistency, we must flush
   * any pending writes prior to a read operation.  This is a const
   * version of flush with some different logging to help detect
   * and protect against this. */
  void flushForRead() const;

  /**
   * Flushes the writeBatch if batch loading mode is not enabled, or
   * if the writeBatchBufferSize_ is exceeded */
  void flushIfNotBatch();

  std::unique_ptr<rocksdb::DB> db_;

  struct PendingWrite {
    /**
     * We need to track this via a pointer to avoid pulling in the full
     * rocksdb headers */
    std::unique_ptr<rocksdb::WriteBatch> writeBatch;
    /**
     * Tracks all of the keys inserted since enableBatchMode() was
     * called. */
    folly::StringKeyedUnorderedSet batchedKeys;
  };
  mutable folly::Synchronized<PendingWrite> pending_;

  /**
   * Controls whether we are in batch mode or not.
   * 0 means no, otherwise it holds the size of the buffer to use for batching.
   */
  size_t writeBatchBufferSize_{0};
};
}
}
