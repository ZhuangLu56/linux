#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/sel4_cap_system.h>
#include <linux/sel4_cslot_allocator.h>

#define SEL4_PAGE_SIZE_BITS 12
#define CONST        __attribute__((__const__))

bool seL4_buddy_root_init_flag = false;
/* use one cnode represent the cspace */
static cte_t root_cnode[MAX_CTE_ENTRIES];

static inline void
cap_untyped_cap_set_capFreeIndex(cap_t *cap, uint64_t v64)
{
	cap->words[1] = (cap->words[1] & ~0xFFFFFFFFFFFFF000ULL) |
			(v64 & 0xFFFFFFFFFFFFF000ULL);
}

static inline cap_tag_t CONST
cap_get_tag(cap_t c)
{
    return (cap_tag_t)((c.words[0] >> 59) & 0x1full);
}

static inline bool CONST
is_untyped_cap(cap_t c)
{
    return cap_get_tag(c) == cap_untyped_cap;
}

static inline bool CONST
is_frame_cap(cap_t c)
{
    return cap_get_tag(c) == cap_frame_cap;
}

static inline cap_t CONST
cap_untyped_cap_new(uint64_t capFreeIndex, uint64_t capIsDevice, uint64_t capBlockSize, uint64_t capPtr) {
    cap_t cap;

    cap.words[0] = 0
        | ((uint64_t)cap_untyped_cap & 0x1full) << 59
        | (capPtr & 0xffffffffffffull) >> 0;
    cap.words[1] = 0
        | (capFreeIndex & 0xffffffffffffull) << 16
        | (capIsDevice & 0x1ull) << 6
        | (capBlockSize & 0x3full) << 0;

    return cap;
}

static inline cap_t CONST
cap_frame_cap_new(uint64_t capFMappedASID, uint64_t capFBasePtr, uint64_t capFSize, uint64_t capFMappedAddress, uint64_t capFVMRights, uint64_t capFIsDevice) {
    cap_t cap;

    cap.words[0] = 0
        | ((uint64_t)cap_frame_cap & 0x1full) << 59
        | (capFSize & 0x3ull) << 57
        | (capFMappedAddress & 0xffffffffffffull) << 9
        | (capFVMRights & 0x3ull) << 7
        | (capFIsDevice & 0x1ull) << 6;
    cap.words[1] = 0
        | (capFMappedASID & 0xffffull) << 48
        | (capFBasePtr & 0xffffffffffffull) >> 0;

    return cap;
}

static inline bool is_cte_null(int idx)
{
    return (idx <= 0 || idx >= MAX_CTE_ENTRIES);
}

static inline void clear_cte(int idx)
{   
    if (is_cte_null(idx)) return;
    memset(&root_cnode[idx].cap, 0, sizeof(cap_t));
    root_cnode[idx].mdb.mdbNext = INVALID_CTE_REF;
    root_cnode[idx].mdb.mdbPrev = INVALID_CTE_REF;
    root_cnode[idx].parent = INVALID_CTE_REF; 
}

/*
 * Insert a new capability into the capability tree(CDT).
 */
static void insertNewCap(int parent_idx, int dest_idx, cap_t newCap)
{
	mdb_node_t srcMDB;
	mdb_node_t newMDB;

	if (!is_cte_null(parent_idx)) {
		srcMDB = root_cnode[parent_idx].mdb;
	} else {
		pr_err("ZhuangL error >>>>>>>>>>>>>>>>>>> insertNewCap: parent_idx %d is null\n",
		       parent_idx);
		return;
	}

    newMDB = srcMDB;
    newMDB.mdbPrev = parent_idx;

    /* set dest slot */
    root_cnode[dest_idx].cap = newCap;
    root_cnode[dest_idx].mdb = newMDB;
    root_cnode[dest_idx].parent = parent_idx;

    /* update links */
    int next_idx = srcMDB.mdbNext;
    root_cnode[parent_idx].mdb.mdbNext = dest_idx;
    if (!is_cte_null(next_idx)) {
        root_cnode[next_idx].mdb.mdbPrev = dest_idx;
    }
    
}

static inline unsigned long getObjectSizeBits(object_t newType, unsigned long userSize)
{
	if (newType == OBJ_PAGE)
		return SEL4_PAGE_SIZE_BITS;
	return userSize;
}

static cap_t createObject(object_t type, void *object_ptr, void *cap_ptr, unsigned long userSize)
{
	cap_t res;
	switch (type) {
	case OBJ_PAGE:
        res = cap_frame_cap_new(0, (unsigned long)object_ptr,
                    0, 0, 0, 0);
		break;
	case OBJ_UNTYPED:
        res = cap_untyped_cap_new(0, 0, userSize, (uint64_t)cap_ptr);
		break;
	default:
		pr_err("ZhuangL error >>>>>>>>>>>>>>>>>>> createObject: unknown type %d\n",
		       type);
	}
	return res;
}

static void createNewObjects(object_t type, int parent_idx, int dest_idx,
			    void *obj_addr, unsigned long userSize)
{
	unsigned long objectSizeBits = getObjectSizeBits(type, userSize);

    cap_t newcap = createObject(type, obj_addr, &root_cnode[dest_idx].cap, objectSizeBits);

    insertNewCap(parent_idx, dest_idx, newcap);
}

/*
* seL4 mircokernel excute path is 'decodeUntypedInvocation' ---> 'invokeUntyped_Retype'
* there is no system call processing in linux kernel, so we implement a function directly
*/
int seL4_Untype_Retype(int srcSlotIdx, int destSlotIdx, object_t newType,
				unsigned long userSize,
                void *object_ptr)
{
	unsigned long totalObjectSize;

	/* calculate size and update free index in untyped cap */
	totalObjectSize = 1 << getObjectSizeBits(newType, userSize);

	/* this just simulate */
	cap_untyped_cap_set_capFreeIndex(&root_cnode[srcSlotIdx].cap,
					 (uint64_t)totalObjectSize);

	/* create objects */
	createNewObjects(newType, srcSlotIdx, destSlotIdx,
			object_ptr, userSize);

	return 0;
}

/*
 * If it's a page cap, we delete it along with all its sibling and parent nodes. 
 * If it's an untyped cap, we only need to remove that particular node. 
 * In the end, we need to return the cslot_idx of the parent or grandparent node 
 * which is used to update the cslot_idx of the page structure, because the cslot_idx of the page structure is reusable.
 */
int seL4_CNode_Delete(int slot_idx)
{
    if(is_cte_null(slot_idx)) {
        pr_err("ZhuangL error >>>>>>>>>>>>>>>>>>> %s: slot_idx %d is null\n", __func__, slot_idx);
        return INVALID_CTE_REF;
    }

    int parent = root_cnode[slot_idx].parent;

    /* it is a root node, we shouldn't delete it */
    if(is_cte_null(parent)) {
        return slot_idx;
    }

    int grand_parent = root_cnode[parent].parent;

    /* delete all child page cap */
    if(is_frame_cap(root_cnode[slot_idx].cap)) {
        int i = 0;
        int cur = root_cnode[parent].mdb.mdbNext;

        while (!is_cte_null(cur) && root_cnode[cur].parent == parent) {
            int next = root_cnode[cur].mdb.mdbNext;
            sel4_cslot_free(cur);
            clear_cte(cur);
            cur = next;
        }
        
        /* update the parent links */
        root_cnode[parent].mdb.mdbNext = cur;
    }

    /* parent is the root node, we shouldn't delete it */
    if(is_cte_null(grand_parent)) {
        return parent;
    }

    /* get the parent's sibling node */
    int sibling = root_cnode[grand_parent].mdb.mdbNext;
    if(sibling == parent) {
        sibling = root_cnode[parent].mdb.mdbNext;
    } 

    /* update the grand_parent links */
    root_cnode[grand_parent].mdb.mdbNext = sibling;

    sel4_cslot_free(parent);
    clear_cte(parent);
    return grand_parent;

}

/*
 * Init the root cnode, clear all cte entries 
 */
void seL4_init_root_cnode(void)
{   
    memset(root_cnode, 0, sizeof(root_cnode));
    int i;
    for (i = 0; i < MAX_CTE_ENTRIES; i++) {
        clear_cte(i);
    }
}

/*
* It similate the seL4 rootserver, init the root node page
*/
void seL4_buddy_root_page_init(struct page *page, int cte_index, unsigned long userSize)
{   
    root_cnode[cte_index].cap = createObject(OBJ_UNTYPED, (void *)page, &root_cnode[cte_index].cap, userSize);
    root_cnode[cte_index].mdb.mdbNext = INVALID_CTE_REF;
    root_cnode[cte_index].mdb.mdbPrev = INVALID_CTE_REF;
    root_cnode[cte_index].parent = INVALID_CTE_REF;
}
