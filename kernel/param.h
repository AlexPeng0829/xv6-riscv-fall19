#define NPROC        10  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NVMA         12  // number of Virtual Memory Area used by mmap and munmap
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       0  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       2000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define NDISK        2

#define PROT_READ  0x1
#define PROT_WRITE 0x2

#define MAP_PRIVATE 0x1
#define MAP_SHARED  0x2