/*
 * Tencent is pleased to support the open source community by making TBase available.  
 * 
 * Copyright (C) 2019 Tencent.  All rights reserved.
 * 
 * TBase is licensed under the BSD 3-Clause License, except for the third-party component listed below. 
 * 
 * A copy of the BSD 3-Clause License is included in this file.
 * 
 * Other dependencies and licenses:
 * 
 * Open Source Software Licensed Under the PostgreSQL License: 
 * --------------------------------------------------------------------
 * 1. Postgres-XL XL9_5_STABLE
 * Portions Copyright (c) 2015-2016, 2ndQuadrant Ltd
 * Portions Copyright (c) 2012-2015, TransLattice, Inc.
 * Portions Copyright (c) 2010-2017, Postgres-XC Development Group
 * Portions Copyright (c) 1996-2015, The PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, The Regents of the University of California
 * 
 * Terms of the PostgreSQL License: 
 * --------------------------------------------------------------------
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written agreement
 * is hereby granted, provided that the above copyright notice and this
 * paragraph and the following two paragraphs appear in all copies.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 * 
 * 
 * Terms of the BSD 3-Clause License:
 * --------------------------------------------------------------------
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of Tencent nor the names of its contributors may be used to endorse or promote products derived from this software without 
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE 
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH 
 * DAMAGE.
 * 
 */
/*-------------------------------------------------------------------------
 *
 * buf_internals.h
 *      Internal definitions for buffer manager and the buffer replacement
 *      strategy.
 *
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/storage/buf_internals.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef BUFMGR_INTERNALS_H
#define BUFMGR_INTERNALS_H

#include "storage/buf.h"
#include "storage/bufmgr.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "storage/smgr.h"
#include "port/atomics.h"
#include "storage/spin.h"
#include "utils/relcache.h"


/*
 * Buffer state is a single 32-bit variable where following data is combined.
 *
 * - 18 bits refcount
 * - 4 bits usage count
 * - 10 bits of flags
 *
 * Combining these values allows to perform some operations without locking
 * the buffer header, by modifying them together with a CAS loop.
 *
 * The definition of buffer state components is below.
 */
#define BUF_REFCOUNT_ONE 1
#define BUF_REFCOUNT_MASK ((1U << 18) - 1)
#define BUF_USAGECOUNT_MASK 0x003C0000U
#define BUF_USAGECOUNT_ONE (1U << 18)
#define BUF_USAGECOUNT_SHIFT 18
#define BUF_FLAG_MASK 0xFFC00000U

/* Get refcount and usagecount from buffer state */
#define BUF_STATE_GET_REFCOUNT(state) ((state) & BUF_REFCOUNT_MASK)
#define BUF_STATE_GET_USAGECOUNT(state) (((state) & BUF_USAGECOUNT_MASK) >> BUF_USAGECOUNT_SHIFT)

/*
 * Flags for buffer descriptors
 *
 * Note: TAG_VALID essentially means that there is a buffer hashtable
 * entry associated with the buffer's tag.
 */
#define BM_LOCKED                (1U << 22)    /* buffer header is locked */
#define BM_DIRTY                (1U << 23)    /* data needs writing */
#define BM_VALID                (1U << 24)    /* data is valid */
#define BM_TAG_VALID            (1U << 25)    /* tag is assigned */
#define BM_IO_IN_PROGRESS        (1U << 26)    /* read or write in progress */
#define BM_IO_ERROR                (1U << 27)    /* previous I/O failed */
#define BM_JUST_DIRTIED            (1U << 28)    /* dirtied since write started */
#define BM_PIN_COUNT_WAITER        (1U << 29)    /* have waiter for sole pin */
#define BM_CHECKPOINT_NEEDED    (1U << 30)    /* must write for checkpoint */
#define BM_PERMANENT            (1U << 31)    /* permanent buffer (not unlogged,
                                             * or init fork) */
/*
 * The maximum allowed value of usage_count represents a tradeoff between
 * accuracy and speed of the clock-sweep buffer management algorithm.  A
 * large value (comparable to NBuffers) would approximate LRU semantics.
 * But it can take as many as BM_MAX_USAGE_COUNT+1 complete cycles of
 * clock sweeps to find a free buffer, so in practice we don't want the
 * value to be very large.
 */
#define BM_MAX_USAGE_COUNT    5

/*
 * Buffer tag identifies which disk block the buffer contains.
 *
 * Note: the BufferTag data must be sufficient to determine where to write the
 * block, without reference to pg_class or pg_tablespace entries.  It's
 * possible that the backend flushing the buffer doesn't even believe the
 * relation is visible yet (its xact may have started before the xact that
 * created the rel).  The storage manager must be able to cope anyway.
 *
 * Note: if there's any pad bytes in the struct, INIT_BUFFERTAG will have
 * to be fixed to zero them, since this struct is used as a hash key.
 */
typedef struct buftag
{
    RelFileNode rnode;            /* physical relation identifier */
    ForkNumber    forkNum;
    BlockNumber blockNum;        /* blknum relative to begin of reln */
} BufferTag;

#define CLEAR_BUFFERTAG(a) \
( \
    (a).rnode.spcNode = InvalidOid, \
    (a).rnode.dbNode = InvalidOid, \
    (a).rnode.relNode = InvalidOid, \
    (a).forkNum = InvalidForkNumber, \
    (a).blockNum = InvalidBlockNumber \
)

#define INIT_BUFFERTAG(a,xx_rnode,xx_forkNum,xx_blockNum) \
( \
    (a).rnode = (xx_rnode), \
    (a).forkNum = (xx_forkNum), \
    (a).blockNum = (xx_blockNum) \
)

#define BUFFERTAGS_EQUAL(a,b) \
( \
    RelFileNodeEquals((a).rnode, (b).rnode) && \
    (a).blockNum == (b).blockNum && \
    (a).forkNum == (b).forkNum \
)

/*
 * The shared buffer mapping table is partitioned to reduce contention.
 * To determine which partition lock a given tag requires, compute the tag's
 * hash code with BufTableHashCode(), then apply BufMappingPartitionLock().
 * NB: NUM_BUFFER_PARTITIONS must be a power of 2!
 */
#define BufTableHashPartition(hashcode) \
    ((hashcode) % NUM_BUFFER_PARTITIONS)
#define BufMappingPartitionLock(hashcode) \
    (&MainLWLockArray[BUFFER_MAPPING_LWLOCK_OFFSET + \
        BufTableHashPartition(hashcode)].lock)
#define BufMappingPartitionLockByIndex(i) \
    (&MainLWLockArray[BUFFER_MAPPING_LWLOCK_OFFSET + (i)].lock)

/*
 *    BufferDesc -- shared descriptor/state data for a single shared buffer.
 *
 * Note: Buffer header lock (BM_LOCKED flag) must be held to examine or change
 * the tag, state or wait_backend_pid fields.  In general, buffer header lock
 * is a spinlock which is combined with flags, refcount and usagecount into
 * single atomic variable.  This layout allow us to do some operations in a
 * single atomic operation, without actually acquiring and releasing spinlock;
 * for instance, increase or decrease refcount.  buf_id field never changes
 * after initialization, so does not need locking.  freeNext is protected by
 * the buffer_strategy_lock not buffer header lock.  The LWLock can take care
 * of itself.  The buffer header lock is *not* used to control access to the
 * data in the buffer!
 *
 * It's assumed that nobody changes the state field while buffer header lock
 * is held.  Thus buffer header lock holder can do complex updates of the
 * state variable in single write, simultaneously with lock release (cleaning
 * BM_LOCKED flag).  On the other hand, updating of state without holding
 * buffer header lock is restricted to CAS, which insure that BM_LOCKED flag
 * is not set.  Atomic increment/decrement, OR/AND etc. are not allowed.
 *
 * An exception is that if we have the buffer pinned, its tag can't change
 * underneath us, so we can examine the tag without locking the buffer header.
 * Also, in places we do one-time reads of the flags without bothering to
 * lock the buffer header; this is generally for situations where we don't
 * expect the flag bit being tested to be changing.
 *
 * We can't physically remove items from a disk page if another backend has
 * the buffer pinned.  Hence, a backend may need to wait for all other pins
 * to go away.  This is signaled by storing its own PID into
 * wait_backend_pid and setting flag bit BM_PIN_COUNT_WAITER.  At present,
 * there can be only one such waiter per buffer.
 *
 * We use this same struct for local buffer headers, but the locks are not
 * used and not all of the flag bits are useful either. To avoid unnecessary
 * overhead, manipulations of the state field should be done without actual
 * atomic operations (i.e. only pg_atomic_read_u32() and
 * pg_atomic_unlocked_write_u32()).
 *
 * Be careful to avoid increasing the size of the struct when adding or
 * reordering members.  Keeping it below 64 bytes (the most common CPU
 * cache line size) is fairly important for performance.
 */
typedef struct BufferDesc
{
    BufferTag    tag;            /* ID of page contained in buffer */
    int            buf_id;            /* buffer's index number (from 0) */

    /* state of the tag, containing flags, refcount and usagecount */
    pg_atomic_uint32 state;


    int            wait_backend_pid;    /* backend PID of pin-count waiter */
    int            freeNext;        /* link in freelist chain */

    LWLock        content_lock;    /* to lock access to buffer contents */
} BufferDesc;

/*
 * Concurrent access to buffer headers has proven to be more efficient if
 * they're cache line aligned. So we force the start of the BufferDescriptors
 * array to be on a cache line boundary and force the elements to be cache
 * line sized.
 *
 * XXX: As this is primarily matters in highly concurrent workloads which
 * probably all are 64bit these days, and the space wastage would be a bit
 * more noticeable on 32bit systems, we don't force the stride to be cache
 * line sized on those. If somebody does actual performance testing, we can
 * reevaluate.
 *
 * Note that local buffer descriptors aren't forced to be aligned - as there's
 * no concurrent access to those it's unlikely to be beneficial.
 *
 * We use 64bit as the cache line size here, because that's the most common
 * size. Making it bigger would be a waste of memory. Even if running on a
 * platform with either 32 or 128 byte line sizes, it's good to align to
 * boundaries and avoid false sharing.
 */
#define BUFFERDESC_PAD_TO_SIZE    (SIZEOF_VOID_P == 8 ? 64 : 1)

typedef union BufferDescPadded
{
    BufferDesc    bufferdesc;
    char        pad[BUFFERDESC_PAD_TO_SIZE];
} BufferDescPadded;

#define GetBufferDescriptor(id) (&BufferDescriptors[(id)].bufferdesc)
#define GetLocalBufferDescriptor(id) (&LocalBufferDescriptors[(id)])

#define BufferDescriptorGetBuffer(bdesc) ((bdesc)->buf_id + 1)

#define BufferDescriptorGetIOLock(bdesc) \
    (&(BufferIOLWLockArray[(bdesc)->buf_id]).lock)
#define BufferDescriptorGetContentLock(bdesc) \
    ((LWLock*) (&(bdesc)->content_lock))

extern PGDLLIMPORT LWLockMinimallyPadded *BufferIOLWLockArray;

/*
 * The freeNext field is either the index of the next freelist entry,
 * or one of these special values:
 */
#define FREENEXT_END_OF_LIST    (-1)
#define FREENEXT_NOT_IN_LIST    (-2)

/*
 * Functions for acquiring/releasing a shared buffer header's spinlock.  Do
 * not apply these to local buffers!
 */
extern uint32 LockBufHdr(BufferDesc *desc);
#define UnlockBufHdr(desc, s)    \
    do {    \
        pg_write_barrier(); \
        pg_atomic_write_u32(&(desc)->state, (s) & (~BM_LOCKED)); \
    } while (0)


/*
 * The PendingWriteback & WritebackContext structure are used to keep
 * information about pending flush requests to be issued to the OS.
 */
typedef struct PendingWriteback
{
    /* could store different types of pending flushes here */
    BufferTag    tag;
} PendingWriteback;

/* struct forward declared in bufmgr.h */
typedef struct WritebackContext
{
    /* pointer to the max number of writeback requests to coalesce */
    int           *max_pending;

    /* current number of pending writeback requests */
    int            nr_pending;

    /* pending requests */
    PendingWriteback pending_writebacks[WRITEBACK_MAX_PENDING_FLUSHES];
} WritebackContext;

/* in buf_init.c */
extern PGDLLIMPORT BufferDescPadded *BufferDescriptors;
extern PGDLLIMPORT WritebackContext BackendWritebackContext;

/* in localbuf.c */
extern BufferDesc *LocalBufferDescriptors;

/* in bufmgr.c */
#ifdef _MLS_
typedef struct tagSyncBufIdInfo
{
    int buf_id;
    int slot_id;
    int worker_id;
    int status;
    char* encrypted_buf;
}SyncBufIdInfo;
#endif


/*
 * Structure to sort buffers per file on checkpoints.
 *
 * This structure is allocated per buffer in shared memory, so it should be
 * kept as small as possible.
 */
typedef struct CkptSortItem
{
    Oid            tsId;
    Oid            relNode;
    ForkNumber    forkNum;
    BlockNumber blockNum;
    int            buf_id;
} CkptSortItem;

extern CkptSortItem *CkptBufferIds;

/*
 * Internal buffer management routines
 */
/* bufmgr.c */
extern void WritebackContextInit(WritebackContext *context, int *max_pending);
extern void IssuePendingWritebacks(WritebackContext *context);
extern void ScheduleBufferTagForWriteback(WritebackContext *context, BufferTag *tag);

/* freelist.c */
extern BufferDesc *StrategyGetBuffer(BufferAccessStrategy strategy,
                  uint32 *buf_state);
extern void StrategyFreeBuffer(BufferDesc *buf);
extern bool StrategyRejectBuffer(BufferAccessStrategy strategy,
                     BufferDesc *buf);

extern int    StrategySyncStart(uint32 *complete_passes, uint32 *num_buf_alloc);
extern void StrategyNotifyBgWriter(int bgwprocno);

extern Size StrategyShmemSize(void);
extern void StrategyInitialize(bool init);

/* buf_table.c */
extern Size BufTableShmemSize(int size);
extern void InitBufTable(int size);
extern uint32 BufTableHashCode(BufferTag *tagPtr);
extern int    BufTableLookup(BufferTag *tagPtr, uint32 hashcode);
extern int    BufTableInsert(BufferTag *tagPtr, uint32 hashcode, int buf_id);
extern void BufTableDelete(BufferTag *tagPtr, uint32 hashcode);

/* localbuf.c */
extern void LocalPrefetchBuffer(SMgrRelation smgr, ForkNumber forkNum,
                    BlockNumber blockNum);
extern BufferDesc *LocalBufferAlloc(SMgrRelation smgr, ForkNumber forkNum,
                 BlockNumber blockNum, bool *foundPtr);
extern void MarkLocalBufferDirty(Buffer buffer);
extern void DropRelFileNodeLocalBuffers(RelFileNode rnode, ForkNumber forkNum,
                            BlockNumber firstDelBlock);
extern void DropRelFileNodeAllLocalBuffers(RelFileNode rnode);
extern void AtEOXact_LocalBuffers(bool isCommit);

#ifdef _MLS_
extern char * BufHdrGetBlockFunc(BufferDesc *buf);
#endif

extern void BufEnableMemoryProtection(char *address, bool localbuffer);
extern void BufDisableMemoryProtection(char *address, bool localbuffer);
#endif                            /* BUFMGR_INTERNALS_H */
