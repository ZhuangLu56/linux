#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/mm.h>

/*
 * This is a simple seL4 Libos-style single-level cslot allocator implementation, copy form sel4.
 */

#define MAX_CSLOTS (2 * 1024 * 1024) /* Max number of slots */
#define INVALID_CAP_ID 0 /* Invalid cap_id(cslots) */
#define BITS_PER_WORD (sizeof(unsigned long) * 8) 

struct sel4_cslot_allocator {
    
    unsigned long bitmap[BITS_TO_LONGS(MAX_CSLOTS)];
    size_t bitmap_length;   
    size_t last_entry;    
    
    spinlock_t lock;
};

extern struct sel4_cslot_allocator global_cslot_allocator;
extern bool cslot_allocator_init;

/* seL4-style helper macros */
#define CLZL(x) __builtin_clzl(x)
#define BIT_UL(nr) (1UL << (nr))

/* cslot allocator system interface */
int sel4_cslot_allocator_init(void);
void sel4_cslot_allocator_cleanup(void);
int sel4_cslot_alloc(void);
void sel4_cslot_free(unsigned long cslot_id);
inline void sel4_set_page_cslot_id(struct page *page, unsigned long cslot_id);
inline unsigned long sel4_get_page_cslot_id(struct page *page);
