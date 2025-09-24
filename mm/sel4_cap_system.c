#include <linux/sel4_cap_system.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/mm.h>

struct sel4_cspace global_cspace;
bool buddy_cap_init = false;
#define ZHUANGL_DEBUG 0 /* Debug level */

/*
* Init the cap system
*/
int sel4_cap_system_init(void)
{
    int i;
    
    spin_lock_init(&global_cspace.lock);
    
    /* Initialize all capabilities */
    for (i = 0; i < MAX_CAPS; i++) {
        global_cspace.caps[i].object = NULL;
        global_cspace.caps[i].size = 0;
        global_cspace.caps[i].valid = 0;
    }
    
    global_cspace.bitmap_length = BITS_TO_LONGS(MAX_CAPS);
    
    bitmap_fill(global_cspace.bitmap, MAX_CAPS);
    
    /* Reserve cap_id 0 as invalid, because struct page's cap_id is default 0 */
    clear_bit(0, global_cspace.bitmap);
    
    global_cspace.last_entry = 0;
    
    buddy_cap_init = true;
    
    pr_info("ZhuangL debug: %s >>>>>>>>>>>>>>> seL4-style single level cap system created (bitmap_words: %lu)\n", 
            __func__, global_cspace.bitmap_length);
    return 0;
}

/*
* Cleanup the cap system, but is not used currently
*/
void sel4_cap_system_cleanup(void)
{
    unsigned long flags;
    int i;
    
    spin_lock_irqsave(&global_cspace.lock, flags);
    
    /* Clear all capabilities */
    for (i = 0; i < MAX_CAPS; i++) {
        global_cspace.caps[i].object = NULL;
        global_cspace.caps[i].size = 0;
        global_cspace.caps[i].valid = 0;
    }
    
    /* Clear bitmap */
    bitmap_zero(global_cspace.bitmap, MAX_CAPS);
    global_cspace.last_entry = 0;
    
    buddy_cap_init = false;
    spin_unlock_irqrestore(&global_cspace.lock, flags);
    pr_info("ZhuangL debug: %s >>>>>>>>>>>>>>> seL4-style single level cap system destroyed\n", __func__);
}

/*
* Allocate a capability for struct page
*/
int sel4_cap_alloc(void *object, unsigned long size)
{   
     unsigned long flags;
    size_t i;
    unsigned long cap_id;
    int bit_index;
    
    if (!buddy_cap_init) {
        pr_err("ZhuangL error: %s >>>>>>>>>>>>>>> cap system not initialized\n", __func__);
        return INVALID_CAP_ID;
    }
    
    spin_lock_irqsave(&global_cspace.lock, flags);
    
    i = global_cspace.last_entry;
    
    if (global_cspace.bitmap[i] == 0) {
        size_t start_entry = i;
        do {
            i = (i + 1) % global_cspace.bitmap_length;
        } while (global_cspace.bitmap[i] == 0 && i != start_entry);
        
        if (i == start_entry && global_cspace.bitmap[i] == 0) {
            /* No available slots found */
            spin_unlock_irqrestore(&global_cspace.lock, flags);
            pr_err("ZhuangL error: %s >>>>>>>>>>>>>>> cap space exhausted\n", __func__);
            return INVALID_CAP_ID;
        }
        
        global_cspace.last_entry = i;
    }
    
    bit_index = BITS_PER_WORD - 1 - CLZL(global_cspace.bitmap[i]);
    
    cap_id = i * BITS_PER_WORD + bit_index;
    
    if (cap_id >= MAX_CAPS) {
        spin_unlock_irqrestore(&global_cspace.lock, flags);
        pr_err("ZhuangL error: %s >>>>>>>>>>>>>>> calculated cap_id %lu >= MAX_CAPS\n", __func__, cap_id);
        return INVALID_CAP_ID;
    }
    
    global_cspace.bitmap[i] &= ~BIT_UL(bit_index);
    
    global_cspace.caps[cap_id].object = object;
    global_cspace.caps[cap_id].size = size;
    global_cspace.caps[cap_id].valid = 1;
    
    spin_unlock_irqrestore(&global_cspace.lock, flags);
    
    if (ZHUANGL_DEBUG) {
        pr_info("ZhuangL debug: %s >>>>>>>>>>>>>>> allocated cap_id=%lu for object=%p, size=%lu\n", 
                __func__, cap_id, object, size);
    }

    return cap_id;
}

void sel4_cap_free(unsigned long cap_id)
{
   unsigned long flags;
    unsigned long word_index;
    unsigned long bit_index;
    
    if (!buddy_cap_init) {
        pr_err("ZhuangL error: %s >>>>>>>>>>>>>>> cap system not initialized\n", __func__);
        return;
    }
    
    if (cap_id == INVALID_CAP_ID || cap_id >= MAX_CAPS) {
        pr_err("ZhuangL error: %s >>>>>>>>>>>>>>> invalid cap_id=%lu\n", __func__, cap_id);
        return;
    }
    
    word_index = cap_id / BITS_PER_WORD;
    bit_index = cap_id % BITS_PER_WORD;
    
    spin_lock_irqsave(&global_cspace.lock, flags);
    
    /* Check if it was allocated (bit should be 0) */
    if (global_cspace.bitmap[word_index] & BIT_UL(bit_index)) {
        pr_err("ZhuangL debug: %s >>>>>>>>>>>>>>> cap_id=%lu was not allocated\n", __func__, cap_id);
    }
    
    global_cspace.bitmap[word_index] |= BIT_UL(bit_index);
    
    global_cspace.caps[cap_id].object = NULL;
    global_cspace.caps[cap_id].size = 0;
    global_cspace.caps[cap_id].valid = 0;
    
    spin_unlock_irqrestore(&global_cspace.lock, flags);

    if (ZHUANGL_DEBUG) {
        pr_info("ZhuangL debug: %s >>>>>>>>>>>>>>> freed cap_id=%lu\n", __func__, cap_id);
    }
}

inline void sel4_set_page_cap_id(struct page *page, unsigned long cap_id)
{
    page->sel4_cap_id = cap_id;
}

inline unsigned long sel4_get_page_cap_id(struct page *page)
{
    return page->sel4_cap_id;
}
