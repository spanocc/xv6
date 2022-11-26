struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  int ticks;
  //struct buf *prev; // LRU cache list
  struct buf *next;   //只需要单向链表即可，通过ticks来找LRU
  uchar data[BSIZE];
};

