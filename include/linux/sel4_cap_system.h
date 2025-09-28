#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/mm.h>

#define MAX_CTE_ENTRIES (2 * 1024 * 1024)
#define INVALID_CTE_REF (-1) /* Invalid CTE reference */

typedef enum { 
	OBJ_PAGE, 
	OBJ_UNTYPED 
} object_t;

typedef enum { 
	cap_frame_cap = 1, 
	cap_untyped_cap = 2 
} cap_tag_t;

/* seL4-style capability structure */
typedef struct {
	uint64_t words[2];
} cap_t;

/* 
*  we use the the index of root_cnode as the cte ptr to create the CDT(Capability Derivation Tree)
*  for example, the CDT like this:
*	A
*	├─ B
*	│   mdbPrev -> A
*	│   mdbNext -> D
*	│
*	│   └─ D
*	│       mdbPrev -> B
*	│       mdbNext -> C
*	└─ C
*	│	mdbPrev -> D
*	│	mdbNext -> E
*	│   └─ E
*	│       mdbPrev -> C
*	│       mdbNext -> NULL
*/
typedef struct {
	int mdbNext;
	int mdbPrev;
} mdb_node_t;

typedef struct {
	cap_t cap;
	mdb_node_t mdb;
	int parent; /* convenient for finding parent nodes */
} cte_t;

extern bool seL4_buddy_root_init_flag;

void seL4_init_root_cnode(void);

void seL4_buddy_root_page_init(struct page *page, int cte_index,
			       unsigned long userSize);

int seL4_Untype_Retype(int srcSlotIdx, int destSlotIdx, object_t newType,
		       unsigned long userSize, void *object_ptr);

int seL4_CNode_Delete(int slot_idx);