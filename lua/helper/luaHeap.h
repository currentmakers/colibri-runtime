

#ifndef LUA_HELPER_LUAHEAP_H_
#define LUA_HELPER_LUAHEAP_H_

/**
 * Allocates memory.
 *
 * @param xWantedSize The size to allocate.
 */
void *luaMallocFunction( size_t xWantedSize );

/**
 * Releases a memory block.
 *
 * @param pv Pointer to the memory block.
 */
void luaFreeFunction( void *pv );

/**
 * Returns the remaining space on the heap.
 */
size_t luaGetFreeHeapSize( void );

/**
 * Returns min. remaining space on the heap.
 */
size_t luaGetMinimumEverFreeHeapSize(void);

/**
 * Returns the base address of the backing heap buffer.
 */
void *luaGetHeapBase(void);

/**
 * Returns the total size (in bytes) of the backing heap buffer.
 */
size_t luaGetHeapTotalSize(void);

#endif /* LUA_HELPER_LUAHEAP_H_ */
