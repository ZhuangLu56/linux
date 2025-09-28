#include <linux/sel4_cslot_allocator.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/mm.h>

struct sel4_cslot_allocator global_cslot_allocator;
bool cslot_allocator_init = false;
#define ZHUANGL_DEBUG 0 /* Debug level */

/*
* Init the cslot allocator
*/
int sel4_cslot_allocator_init(void)
{
	spin_lock_init(&global_cslot_allocator.lock);

	global_cslot_allocator.bitmap_length = BITS_TO_LONGS(MAX_CSLOTS);
	bitmap_zero(global_cslot_allocator.bitmap, MAX_CSLOTS);
	/* Reserve cap_id 0 as invalid, because struct page's cap_id is default 0 */
	set_bit(0, global_cslot_allocator.bitmap);
	global_cslot_allocator.last_entry = 0;
	cslot_allocator_init = true;

	pr_info("ZhuangL debug: %s >>>>>>>>>>>>>>> seL4-style single level cap system created (bitmap_words: %lu)\n",
		__func__, global_cslot_allocator.bitmap_length);
	return 0;
}

/*
* Cleanup the cslot allocator, but is not used currently
*/
void sel4_cslot_allocator_cleanup(void)
{
	unsigned long flags;

	spin_lock_irqsave(&global_cslot_allocator.lock, flags);

	/* Clear bitmap */
	bitmap_zero(global_cslot_allocator.bitmap, MAX_CSLOTS);
	set_bit(0, global_cslot_allocator.bitmap);
	global_cslot_allocator.last_entry = 0;
	cslot_allocator_init = false;

	spin_unlock_irqrestore(&global_cslot_allocator.lock, flags);

	pr_info("ZhuangL debug: %s >>>>>>>>>>>>>>> seL4-style single level cap system destroyed\n",
		__func__);
}

/*
* Allocate a cslot, that index of the cnode to store capability
*/
int sel4_cslot_alloc()
{
	unsigned long flags;
	unsigned long cslot_id;
	int bit_index;
	int i;

	if (!cslot_allocator_init) {
		pr_err("ZhuangL error: %s >>>>>>>>>>>>>>> cap system not initialized\n",
		       __func__);
		return INVALID_CAP_ID;
	}

	spin_lock_irqsave(&global_cslot_allocator.lock, flags);

	i = global_cslot_allocator.last_entry;

	if (global_cslot_allocator.bitmap[i] == ~0UL) {
		size_t start_entry = i;
		do {
			i = (i + 1) % global_cslot_allocator.bitmap_length;
		} while (global_cslot_allocator.bitmap[i] == ~0UL &&
			 i != start_entry);

		if (i == start_entry &&
		    global_cslot_allocator.bitmap[i] == ~0UL) {
			/* No available slots found */
			spin_unlock_irqrestore(&global_cslot_allocator.lock,
					       flags);
			pr_err("ZhuangL error: %s >>>>>>>>>>>>>>> cap space exhausted\n",
			       __func__);
			return INVALID_CAP_ID;
		}

		global_cslot_allocator.last_entry = i;
	}

	bit_index = __builtin_ctzl(~global_cslot_allocator.bitmap[i]);

	cslot_id = i * BITS_PER_WORD + bit_index;

	if (cslot_id >= MAX_CSLOTS) {
		spin_unlock_irqrestore(&global_cslot_allocator.lock, flags);
		pr_err("ZhuangL error: %s >>>>>>>>>>>>>>> calculated cslot_id %lu >= MAX_CSLOTS\n",
		       __func__, cslot_id);
		return INVALID_CAP_ID;
	}

	global_cslot_allocator.bitmap[i] |= BIT_UL(bit_index);

	spin_unlock_irqrestore(&global_cslot_allocator.lock, flags);

	if (ZHUANGL_DEBUG) {
		pr_info("ZhuangL debug: %s >>>>>>>>>>>>>>> allocated cslot_id=%lu\n",
			__func__, cslot_id);
	}

	return cslot_id;
}

/* 
* free a cslot, that index of the cnode to store capability
*/
void sel4_cslot_free(unsigned long cslot_id)
{
	unsigned long flags;
	unsigned long word_index;
	unsigned long bit_index;

	if (!cslot_allocator_init) {
		pr_err("ZhuangL error: %s >>>>>>>>>>>>>>> cap system not initialized\n",
		       __func__);
		return;
	}

	if (cslot_id == INVALID_CAP_ID || cslot_id >= MAX_CSLOTS) {
		pr_err("ZhuangL error: %s >>>>>>>>>>>>>>> invalid cslot_id=%lu\n",
		       __func__, cslot_id);
		return;
	}

	word_index = cslot_id / BITS_PER_WORD;
	bit_index = cslot_id % BITS_PER_WORD;

	spin_lock_irqsave(&global_cslot_allocator.lock, flags);

	/* Check if it was already freed (bit should be 0 for allocated) */
	if (!(global_cslot_allocator.bitmap[word_index] & BIT_UL(bit_index))) {
		spin_unlock_irqrestore(&global_cslot_allocator.lock, flags);
		return;
	}

	global_cslot_allocator.bitmap[word_index] &= ~BIT_UL(bit_index);

	spin_unlock_irqrestore(&global_cslot_allocator.lock, flags);
	if (ZHUANGL_DEBUG) {
		pr_info("ZhuangL debug: %s >>>>>>>>>>>>>>> freed cslot_id=%lu\n",
			__func__, cslot_id);
	}
}

inline void sel4_set_page_cslot_id(struct page *page, unsigned long cslot_id)
{
	page->sel4_cslot_id = cslot_id;
}

inline unsigned long sel4_get_page_cslot_id(struct page *page)
{
	return page->sel4_cslot_id;
}
