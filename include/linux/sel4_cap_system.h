#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/bitmap.h>

/*
 * Cap system for linux - seL4 single level cspace， in order to quantify the impact of the cap system on Linux, it copy from sel4.
 * Originally, we wanted to use the Linux function-level 'kmalloc' and 'kfree' to replace the seL4 system call-level 'seL4_Untyped_Retype' and 'seL4_CNode_Delete'
 * for creating and destroying capabilities. However, since kmalloc and kfree rely on the 'slab' allocator, and the 'slab' depends on the 'buddy system', 
 * this would create a closed loop, leading to unable to allocate. As a result, we could only initialize the capabilities from the '.bss' segment.
 * 
 */

#define MAX_CAPS (2 * 1024 * 1024) /* Max number of caps */
#define INVALID_CAP_ID 0UL 
#define BITS_PER_WORD (sizeof(unsigned long) * 8)

struct sel4_capability {
    void *object;       /* point the memory object */
    unsigned long size; /* memory size */
    int valid;          /* whether it is valid */
};

struct sel4_cspace {
    struct sel4_capability caps[MAX_CAPS];
    
    unsigned long bitmap[BITS_TO_LONGS(MAX_CAPS)];
    size_t bitmap_length;   
    size_t last_entry;    
    
    spinlock_t lock;
};

extern struct sel4_cspace global_cspace;
extern bool buddy_cap_init;

/* seL4-style helper macros */
#define CLZL(x) __builtin_clzl(x)
#define BIT_UL(nr) (1UL << (nr))

/* cap system interface */
int sel4_cap_system_init(void);
void sel4_cap_system_cleanup(void);
int sel4_cap_alloc(void *object, unsigned long size);
void sel4_cap_free(unsigned long cap_id);
inline void sel4_set_page_cap_id(struct page *page, unsigned long cap_id);
inline unsigned long sel4_get_page_cap_id(struct page *page);
