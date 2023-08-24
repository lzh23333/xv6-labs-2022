// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define BUCKETBUF (NBUF/NBUCKET + 1)
#define DEVPRIME 100003
// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   struct buf head;
// } bcache;

static inline uint hash(uint dev, uint blockno) {
  return (dev * DEVPRIME + blockno) % NBUCKET;
}

struct {
  struct spinlock lock;
  struct buf bucket[NBUCKET][BUCKETBUF];
  struct spinlock bucketlock[NBUCKET];
  uint capacity[NBUCKET]; // indicate block(refcnt = 0) num 
} bcache;

void
binit(void)
{
  char buf[10];
  initlock(&bcache.lock, "bcache");
  for (int i = 0; i < NBUCKET; i++) {
    snprintf(buf, 10, "bcache%d", i);
    initlock(&bcache.bucketlock[i], buf);
    bcache.capacity[i] = BUCKETBUF;
    for (int j = 0; j < BUCKETBUF; j++) {
      initsleeplock(&bcache.bucket[i][j].lock, "buffer");
    }

  }
}


  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }

// Search Item in bucket, and will change refcount
static struct buf *search(uint key, uint dev, uint blockno) {
  struct buf *b = 0;
  acquire(&bcache.bucketlock[key]);
  for (int i = 0; i < BUCKETBUF; i++) {
    if (bcache.bucket[key][i].dev == dev
        && bcache.bucket[key][i].blockno == blockno) {
      b = &bcache.bucket[key][i];
      break;
    }
  }
  if (b) {
    b->refcnt++;
    if (b->refcnt == 1) {
      bcache.capacity[key]--;
    }
  }
  release(&bcache.bucketlock[key]);
  return b;
}



// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint key = hash(dev, blockno);
  b = search(key, dev, blockno);
  // printf("dev=%d, blockno=%d, key=%d, cap=%d\n",
  //         dev, blockno, key, bcache.capacity[key]);
  if (b) goto find;
  //printf("bucket %d not found.\n", key);
  

  // Not find in bucket, maybe in other bucket
  uint bucket = (key + 1) % NBUCKET;
  while (bucket != key && !b) {
    //printf("searching bucket %d...\n", bucket);
    b = search(bucket, dev, blockno);
    bucket = (bucket + 1) % NBUCKET;
  }
  if (b) goto find;
  //printf("other bucket not found.\n");
  
  // Not cached in bcache
  for (int i = 0; i < NBUCKET; i++) {
    bucket = (key + i) % NBUCKET;
    acquire(&bcache.bucketlock[bucket]);
    // Cache in this bucket
    if (bcache.capacity[bucket] > 0) {
      for (int j = 0; j < BUCKETBUF; j++) {
        //printf("block [%d, %d]\n", bucket, j);
        b = &bcache.bucket[bucket][j];
        if (b->refcnt == 0) {
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;
          bcache.capacity[bucket]--;
          release(&bcache.bucketlock[bucket]);
          goto find;
        }
      }
    }
    release(&bcache.bucketlock[bucket]);
  }
  panic("bget: no buffers");

find:
  
  acquiresleep(&b->lock);
  return b;
  
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

void acquire2bucket(uint bucket1, uint bucket2) {
  if (bucket1 > bucket2) acquire2bucket(bucket2, bucket1);
  else {
    acquire(&bcache.bucketlock[bucket1]);
    acquire(&bcache.bucketlock[bucket2]);
  }
}

void release2bucket(uint bucket1, uint bucket2) {
  if (bucket1 > bucket2) {
    release2bucket(bucket2, bucket1);
    return;
  }
  release(&bcache.bucketlock[bucket2]);
  release(&bcache.bucketlock[bucket1]);
}


// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  uint bucket = (uint)(b - &bcache.bucket[0][0]) / BUCKETBUF;
  //uint key = hash(b->dev, b->blockno);
  acquire(&bcache.bucketlock[bucket]);
  b->refcnt--;
  // if (b->refcnt == 0 && bucket != key) {
  //   bcache.capacity[bucket]--;
  //   // reorder key to avoid deadlock
  //   release(&bcache.bucketlock[bucket]);
  //   acquire2bucket(bucket, key);
  //   if (bcache.capacity[key] > 0) {
  //     bcache.capacity[key]--;
  //     for (int i = 0; i < BUCKETBUF; i++) {
  //       if (bcache.bucket[key][i].refcnt == 0) {

  //       }
  //     }
  //   }
  //   release2bucket(bucket, key);

  // }

  
  release(&bcache.bucketlock[bucket]);
}

void
bpin(struct buf *b) {
  uint key = hash(b->dev, b->blockno);
  acquire(&bcache.bucketlock[key]);
  b->refcnt++;
  release(&bcache.bucketlock[key]);
}

void
bunpin(struct buf *b) {
  uint key = hash(b->dev, b->blockno);
  acquire(&bcache.bucketlock[key]);
  b->refcnt--;
  release(&bcache.bucketlock[key]);
}


