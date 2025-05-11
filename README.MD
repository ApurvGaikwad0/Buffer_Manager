# Buffer Manager




## Overview

The goal of this assignment is to implement a buffer manager that manages a fixed number of pages in memory (called page frames) for an existing page file. The buffer manager works in tandem with a storage manager from Assignment 1. Together, they form a **Buffer Pool**—a combination of a page file and a set of memory frames that cache disk pages.

The buffer manager is capable of handling multiple open buffer pools simultaneously (with the restriction that only one buffer pool may be open per page file). Each pool uses a page replacement strategy, determined at initialization. In our implementation, we have provided two replacement strategies: **FIFO** and **LRU**.

This report summarizes the interface and functionality implemented, and it outlines the contributions made by each group member.

---

## Buffer Pool Interface

### Buffer Pool Methods

- **initBufferPool**  
  Initializes a buffer pool with a specified number of page frames using the selected replacement strategy. The pool is linked to an existing page file (which must preexist), and all frames are initially empty. An optional parameter (`stratData`) may be used to pass parameters for the replacement strategy.

- **shutdownBufferPool**  
  Destroys the buffer pool by first flushing all dirty pages (using `forceFlushPool`) to disk and then freeing the allocated memory. It is an error to shut down a pool if any pages remain pinned.

- **forceFlushPool**  
  Writes all dirty pages (with fix count 0) back to disk. This guarantees that all modifications are persisted before the pool is shut down.

### Page Management Methods

- **pinPage**  
  Pins a page (by its page number) into the buffer pool. If the page is already present, its fix count is increased. Otherwise, the page is read from disk into a free (or chosen victim) frame. The function sets the page number and updates the pointer to the frame’s data.

- **unpinPage**  
  Unpins a page by decreasing its fix count, indicating that a client is finished using the page.

- **markDirty**  
  Marks a page as dirty, meaning that it has been modified. Dirty pages are written back to disk when evicted from the pool.

- **forcePage**  
  Forces the current content of a page to be written back to disk immediately.

### Statistics Methods

- **getFrameContents**  
  Returns an array (of size `numPages`) showing the page numbers stored in each frame. Empty frames are represented by the constant `NO_PAGE`.

- **getDirtyFlags**  
  Returns an array (of size `numPages`) where each element is `TRUE` if the corresponding frame is dirty, and `FALSE` otherwise.

- **getFixCounts**  
  Returns an array (of size `numPages`) showing the fix count for each frame (0 for empty frames).

- **getNumReadIO**  
  Returns the total number of pages read from disk since the buffer pool was initialized.

- **getNumWriteIO**  
  Returns the total number of pages written to disk since the buffer pool was initialized.

### Custom Methods

- **strategyForFIFOandLRU**  
  Implements the FIFO and LRU replacement strategies to select a victim frame for eviction.

- **getAttributionArray**  
  Returns an array of strategy attributes (such as timestamps) used by the replacement strategy.

- **freePagesForBuffer**  
  Frees the memory allocated for page frames and other bookkeeping data during shutdown.

- **updateBfrAtrbt**  
  Updates the buffer attribute (e.g., timestamp) for a page frame. This is invoked within `pinPage` to record page usage.

---

## How to Build and Run

The project includes a Makefile to simplify compilation and testing. The Makefile targets are as follows:

- **Compile All Tests:**  
   Run the command:
  make

This compiles two test executables:

- `test1` (which runs test cases from `test_assign2_1.c`)
- `test2` (which runs test cases from `test_assign2_2.c`)

- **Run the Tests:**  
  On a Windows system, after compilation, run:
  ./test1.exe ./test2.exe

(On Unix-like systems, you would run `./test1` and `./test2`.)

- **Clean Up:**  
  To remove the executables, run:
  make clean

- **Structure of the Assignment**

Structure of the Assignment
---
assign2/
├── README.md
├── Makefile
├── buffer_mgr.h
├── buffer_mgr.c
├── buffer_mgr_stat.c
├── buffer_mgr_stat.h
├── dberror.h
├── dberror.c
├── dt.h
├── storage_mgr.h
├── storage_mgr.c
├── test_assign2_1.c
└── test_assign2_2.c
---
- **Conclusion :**

This assignment provided valuable hands-on experience with buffer management in database systems. Our implementation efficiently caches disk pages using FIFO and LRU strategies while tracking I/O operations and page status. The design adheres to the given interface and has been thoroughly tested using the provided test cases. Future enhancements could include additional replacement strategies (e.g., CLOCK, LRU-K) and making the implementation thread-safe.
