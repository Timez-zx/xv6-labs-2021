// // Buffer cache.
// //
// // The buffer cache is a linked list of buf structures holding
// // cached copies of disk block contents.  Caching disk blocks
// // in memory reduces the number of disk reads and also provides
// // a synchronization point for disk blocks used by multiple processes.
// //
// // Interface:
// // * To get a buffer for a particular disk block, call bread.
// // * After changing buffer data, call bwrite to write it to disk.
// // * When done with the buffer, call brelse.
// // * Do not use the buffer after calling brelse.
// // * Only one process at a time can use a buffer,
// //     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock[BUCKETBUM];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[BUCKETBUM];
} bcache;
uint64 glo_tik = 0;

void
binit(void)
{
  struct buf *b;
  for(int i = 0; i < BUCKETBUM; i++){
    initlock(&bcache.lock[i], "bcache");
    //new
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];

  }
  
  // Create linked list of buffers
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->timestamp = glo_tik++;
    b->blockno = 0;
    //new
    b->next = bcache.head[0].next;
    b->prev = &bcache.head[0];

    initsleeplock(&b->lock, "buffer");
    //new
    bcache.head[0].next->prev = b;
    bcache.head[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf *store = 0;
  int now = glo_tik+100;
  int hash = (blockno)%BUCKETBUM;
  acquire(&bcache.lock[hash]);

  // for(int i = 0;i < NBUF;i++){
  //   b = bcache.buf + i;
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     b->timestamp = glo_tik++;
  //     release(&bcache.lock[hash]);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  for(b = bcache.head[hash].next; b != &bcache.head[hash]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->timestamp = glo_tik++;
      release(&bcache.lock[hash]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  while(1){
    now = glo_tik+100;
    for(int i = 0; i < NBUF; i++){
      b = bcache.buf + i;
      if(b->refcnt == 0 && now > b->timestamp){
        now = b->timestamp;
        store = b;
      }
    }
    if(store){
      int Lhash = (store->blockno)%BUCKETBUM;
      if(Lhash != hash)
        acquire(&bcache.lock[Lhash]);
      if(store->refcnt == 0 ){

        store->dev = dev;
        store->blockno = blockno;
        store->valid = 0;
        store->refcnt = 1;
        store->timestamp = glo_tik++;
        //new
        store->next->prev = store->prev;
        store->prev->next = store->next;
        store->next = bcache.head[hash].next;
        store->prev = &bcache.head[hash];
        bcache.head[hash].next->prev = store;
        bcache.head[hash].next = store;

        if(Lhash != hash)
          release(&bcache.lock[Lhash]);
        release(&bcache.lock[hash]);
        acquiresleep(&store->lock);
        return store;
      }
      else{
        if(Lhash != hash)
          release(&bcache.lock[Lhash]);
      }
    }
  }
  panic("bget: no buffers");
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

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  releasesleep(&b->lock);
  int hash = (b->blockno)%BUCKETBUM;
  acquire(&bcache.lock[hash]);
  b->refcnt--;
  if(b->refcnt == 0) {
    // no one is waiting for it.
    b->timestamp = glo_tik++;
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[hash].next;
    b->prev = &bcache.head[hash];
    bcache.head[hash].next->prev = b;
    bcache.head[hash].next = b;
  }
  release(&bcache.lock[hash]);
}

void
bpin(struct buf *b) {
  int hash = (b->blockno)%BUCKETBUM;
  acquire(&bcache.lock[hash]);
  b->refcnt++;
  release(&bcache.lock[hash]);
}

void
bunpin(struct buf *b) {
  int hash = (b->blockno)%BUCKETBUM;
  acquire(&bcache.lock[hash]);
  b->refcnt--;
  release(&bcache.lock[hash]);
}

// 实验心得：本次实验，第一个实验比较简单，基本上一上午时间就可以写好，而第二个实验，让我吃了很大的苦头，足足写了2天的时间，
//  首先，一开始的设计， 所有hash table的元素，全局访问都是通过链表访问的，实际上， 后面参考别人设计发现，数组直接访问全局更加好，
//  其次，就是并发过程中，各种锁的应用问题，首先， bget和brelse是两个互斥的操作，一个bget就要对应一个brelse，一开始设计的时候
//  在bget中间就释放掉hash锁， 因此导致可能brelse会获取锁，多释放掉hash桶中的数据，导致，一个get对应多个release，这样
//  就会导致freeing free的报错。

// 其次，在进行全局查找的时候，由于是对所有hash桶中的元素进行查找，因此refcnt可能在查找的时候是0，要改变的时候，就不是了，因此
// 在做hash桶切换的之前，要查看这个buf的ref是不是还是0（在查看之前，需要对该least buf上锁，另ref不会在查看时 突然改变），
// 如果改变了不是0，则重新查找，如果不变，则对目标hash桶上锁，完成切换。

// 而 hash桶本身的访问，可以不通过双链表结构，通过数组遍历，就很简单，但是如果将hash table和链表结合起来，访问速度可以提高一些，
// 因为数组访问是全局访问， 而链表访问是基于hash桶的局部访问，对于bget前半部分，局部访问就已经很充足了，所以利用hash table的结构，
// 可以很好的提高访问速度。

// 而在全局进行LRU算法的过程中，之所以没有上锁，首先是因为进程持有一个hash桶的锁，如果在这个过程上锁， 很容易导致死锁， 比如一个进程持有4锁
// 一个进程持有9锁，两个进程都会进行遍历，因此，进程4会要9，进程9会要4， 所以必然死锁，而同时LRU算法过程，只有read操作，找最小LRU，虽然最小
// LRU中的ref以及time会被其他进程影响，但是如果ref后面变了， 就重新查找， 同时，如果ref变了，就意味着time变了，不是最远的，所以重新查找，
// 同时，在进行切换的时候，如果ref为0，则必然为least，因为其他进程的time更新time只会更大，不会更小，因此，在全局搜索的过程中，不对每个桶陆续上锁
// 是合理的，通过上局部锁，我们也没有办法保证全局的time和ref不变，因此，不上锁，后续做切换的时候进行判断，是最好的处理(判断least是否变化)。

//同时，由于存在一个ref会改变，重新查找的过程，一定要更新初始比较时间now， 如果不更新， buf有可能全部大于now，则会导致全局没有least buf， 陷入死循环