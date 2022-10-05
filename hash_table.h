//
// Hash Table
//
// Has a bit of unincluded context.
//


// - true if order is ABC/CAB/BCA, and false if order is CBA/ACB/BAC.
// - If any of A,B,C are the same, the result is false.
#define ThreeCircularIndicesAreAscending(a, b, c) (	\
	(((a)<(b)) + ((b)<(c)) + ((c)<(a))) & 0x2)

// - If C is equal to A or B, this results in false.
// - Otherwise, if A==B this results in true.
// - Otherwise it's the same as above.
#define ThreeCircularIndicesAreAscendingAndFirstTwoCanBeEqual(a, b, c) (	\
	(((a)<=(b)) + ((b)<(c)) + ((c)<(a))) & 0x2)


inline u64 SimpleHash64(u64 a){
	a ^= 0x07B5BAD595E238E31; //  8888888888888888881 (prime)
	a *= 0x09A3298AFB5AC7173; // 11111111111111111027 (prime)
	a ^= a >> 32;
	a *= 0x02E426101834D5517; //  3333333333333333271 (prime)
	return a;
}



//
//
// Hash Table
//
//

/*
 - 'T' can be any struct with these features:
     - A 'key' member that must have a == operator that compares all its bytes.
	   (Must be equivalent to (memcmp(a, b, sizeof(K)) == 0) ).
	   Also, the 'key' type cannot have "filler memory" between members, because
	   we use the raw memory to hash it. You must be proud of all its bytes.

	 - An 'occupied' member, of which you can use all the bits however you want,
	   except the lowest bit, which will be set to 1 when occupied and 0 if no.
	   
 - T Example:
		struct chunk_meta_hashnode{
			union{ u64 key; struct{ u32 chunkX, u32 chunkY}; };
			union{ u32 occupied; u32 flags; }; 
	
			// Other members...
		}

 - Handles collisions by "open addressing".
 
 - Adding/removing a node can move other nodes and invalidate previous pointers to nodes.

 - Added items are zeroed before returned. Removing an item is simply to unset 'occupied'.

 - Only one item per unique key.

 - KEY TYPE SHOULD BE "PACKED". We do memory compares on the keys, so if they have padding
   you'll likely make mistakes (every time you do a Get() you'd have to zero-mem the key).

 - :YouCanDeleteNodesFromAHashtableWhileIteratingIt 
	Since we do open addressing, it's possible that when you delete a node, another node
	will refill that space. That node can come from nodes "after" or "before" that slot.
	And the hole left by moving the new item, can cause other items to move too. But, when
	iterating the hashtable in memory order (FirstOccupied(), Next()) it's guaranteed that
	even if you remove items (the current item of the iteration), you won't miss any. But
	you should not advance if you remove an item and another item fills that slot.
	It's possible that you'll iterate over the same item twice, though.
	This is because if all the next slots to the current are filled, an item at the
	beginning could actually have the current slot as its preferred slot, so it could fill
	it, and then you'll iterate over it twice. But items can't jump from the "end" (after
	your item) to the "beginning" (before your item).
	When you remove items in an iteration, you should call HashTable_RemoveNodeAndGetNext
	instead of HashTable_Next.

*/
template <typename T> struct hash_table{
	s32 totalSlots;
	s32 occupiedSlots;

	void *mem;
};

#define HASHTABLE_MAX_FILLED_FACTOR 0.7f
// It can actually get filled over this factor, but just a bit :D That's why your minimum
// size should be like 10 or something.

// If you want to put no more than n items in a hashtable, how big should you initially
// make it to avoid reallocations?
s32 HashTable_NumTotalSlotsNeededForMaxOccupied(s32 maxOccupied){
	// Resize condition is (occupiedSlots > (s32)(totalSlots * factor))
	// (We add 1 for margin. Otherwise things like GetOrAdd() that choose to resize assuming
	// it will add, might cause a resize even if maxOccupied wouldn't be surpassed...)
	s32 totalSlots = CeilF32ToS32((maxOccupied + 1) / HASHTABLE_MAX_FILLED_FACTOR);

	while((maxOccupied + 1) > (s32)(totalSlots * HASHTABLE_MAX_FILLED_FACTOR)){
		totalSlots++; // Make sure it's enough, because I don't trust the math functions.
	}
	
	return totalSlots;
}

s32 HashTable_NumTotalSlotsToFitMemory(umm memSize, umm typeSize){
	s32 totalSlots = SafeUmmToS32(memSize / typeSize);
	return totalSlots;
}


inline u32 HashTable_U64KeyToHashNotModulo(u64 key){
	u32 result = (u32)SimpleHash64(key);
	return result;
}

template<typename T, typename K>
u32 HashTable_KeyToHash(hash_table<T> *table, K key) {	
	Assert(sizeof(((T *)0)->key) == sizeof(*(K *)0));

	K keyLocal = key;

	u64 asU64;
	if       (sizeof(key) == 1) { asU64 = (u64) *(u8  *)&keyLocal;
	}else if (sizeof(key) == 2) { asU64 = (u64) *(u16 *)&keyLocal;
	}else if (sizeof(key) == 4) { asU64 = (u64) *(u32 *)&keyLocal;
	}else if (sizeof(key) == 8) { asU64 =       *(u64 *)&keyLocal;
	}else{ 							
		asU64 = 0;
		for(u32 i = 0; i < sizeof(key) / 8; i++){
			asU64 ^= ((u64 *)&keyLocal)[i];
		}
		if (sizeof(key) % 8){
			u64 remaining = 0;
			memcpy(&remaining, (u8 *)(&keyLocal + 1) - (sizeof(key) % 8), sizeof(key) % 8);
			asU64 ^= remaining;
		}
	}																			
	u32 hash = HashTable_U64KeyToHashNotModulo(asU64) % table->totalSlots;
	return hash;
}


template <typename T>
T *HashTable_HashToSlot(hash_table<T> *table, u32 hash){
	T *slot = (T *)table->mem + hash;
	return slot;
}

template <typename T, typename K>
T *HashTable_KeyToSlot(hash_table<T> *table, K key){
	Assert(sizeof(((T *)0)->key) == sizeof(*(K *)0));

	u32 hash = HashTable_KeyToHash(table, key);
	T *slot = HashTable_HashToSlot(table, hash);
	return slot;
}

template <typename T>
inline void HashTable_AssertValidType(hash_table<T> *table){
	Assert(sizeof(((T *)0)->occupied)); // Type must have 'occupied' and 'key' members.
	Assert(sizeof(((T *)0)->key ));

	T a = {}, b = {};
	b32 equal = (a.key == b.key);
	if (!equal){
		equal = a.key == b.key;
		InvalidCodepath;
	}
	Assert(a.key == b.key); // K must have a == operator.
}

template <typename T>
inline void HashTable_InitFromMemory(hash_table<T> *table, T *mem, s32 totalSlots){
	Assert(initialNumSlots >= 10);
	HashTable_AssertValidType(table);														

	table->totalSlots = totalSlots;
	table->occupiedSlots = 0;
	table->mem = (void *)mem;
	ZeroSize(table->mem, sizeof(T)*totalSlots);
}
template <typename T>
inline void HashTable_Init(hash_table<T> *table, s32 initialNumSlots) {
	umm memSize = (umm)initialNumSlots*sizeof(T);					
	HashTable_InitFromMemory(table, (T *)malloc(memSize), initialNumSlots);	
}

template <typename T>
inline void HashTable_Destruct(hash_table<T> *table){
	Assert(table->mem);
	free(table->mem);
	ZeroStruct(table);
}

template <typename T>
inline T *HashTable_NextOccupied(hash_table<T> *table, T *element){
	T *limit = (T *)table->mem + table->totalSlots;
	while (1) {
		element++;
		if (element >= limit) return 0;
		if (element->occupied) return element;
	}
}

template <typename T>
inline T *HashTable_FirstOccupied(hash_table<T> *table){
	T *limit = (T *)table->mem + table->totalSlots;
	T *it = (T *)table->mem;
	while (it < limit) {
		if (it->occupied)
			return it;
		it++;
	}
	return 0;
}

template <typename T,typename K>
// USAGE WARNING: CALLING THIS CAN INVALIDATE ANY PREVIOUS ELEMENT POINTERS.
// - The added element is zeroed.
inline T *HashTable_AddNoResize(hash_table<T> *table, K key){
	Assert(sizeof(((T *)0)->key) == sizeof(*(K *)0));
	Assert(table->mem);

	T *directPos = HashTable_KeyToSlot(table, key);
	T *it = directPos;
	T *limit = ((T *)table->mem) + table->totalSlots;
	s32 collisionCount = 0;
	while(1){
		if (!it->occupied){
			ZeroStruct(it);
			it->occupied = 0x1;
			it->key = key;
			table->occupiedSlots++;
			return it;
		}
		if (it->key == key){ // Already present.
			Assert(false);
			// (Try to recover anyway cuz we're nice)
			ZeroStruct(it);
			it->occupied = 0x1;
			it->key = key;
			return it;
		}
		it++;
		collisionCount++;
		if (it >= limit){
			if (limit == directPos){ // Seen all.
				Assert(table->occupiedSlots == table->totalSlots);
				InvalidCodepath;
				return 0;
			}
			// Wrap around
			limit = directPos;
			it = (T *)table->mem;
		}
	}
}

template <typename T>
void HashTable_Resize(hash_table<T> *table, s32 newTotalSlots){
	Assert(newTotalSlots > table->totalSlots);
	hash_table<T> oldTable = *table;

	table->totalSlots = newTotalSlots;
	table->occupiedSlots = 0;

	umm newMemSize = (umm)newTotalSlots*sizeof(T);
	table->mem = malloc(newMemSize);
	ZeroSize(table->mem, newMemSize);

	s32 elementCount = 0;
	for(T *oldIt = HashTable_FirstOccupied(&oldTable);
		oldIt;
		oldIt = HashTable_NextOccupied(&oldTable, oldIt))
	{
		T *element = HashTable_AddNoResize(table, oldIt->key);
		memcpy(element, oldIt, sizeof(T));
		elementCount++;
	}
	Assert(elementCount == oldTable.occupiedSlots);
	Assert(elementCount == table->occupiedSlots);
	free(oldTable.mem);
}

template <typename T, typename K>
// USAGE WARNING: CALLING THIS CAN INVALIDATE ANY PREVIOUS ELEMENT POINTERS.
// - The added element is zeroed.
T *HashTable_Add(hash_table<T> *table, K key){
	Assert(sizeof(((T *)0)->key) == sizeof(*(K *)0));
	Assert(table->mem);

	if (table->occupiedSlots + 1 > (s32)table->totalSlots*HASHTABLE_MAX_FILLED_FACTOR){
		s32 newTotalSlots = table->totalSlots << 1;
		HashTable_Resize(table, newTotalSlots);
	}

	T *result = HashTable_AddNoResize(table, key);
	return result;
}

template <typename T, typename K>
// - 0 if not found.
T *HashTable_Get(hash_table<T> *table, K key){
	Assert(sizeof(((T *)0)->key) == sizeof(*(K *)0));
	Assert(table->mem);

	T *directPos = HashTable_KeyToSlot(table, key);
	T *it = directPos;
	T *limit = (T *)table->mem + table->totalSlots;
	while(1){
		if (!it->occupied){
			return 0; // Not found.
		}
		if (it->key == key){
			return it; // Found.
		}
		it++;
		if (it >= limit){
			if (limit == directPos){
				Assert(table->occupiedSlots == table->totalSlots);
				InvalidCodepath; 
				return 0; // Full, not found.
			}
			// Wrap around.
			limit = directPos;
			it = (T *)table->mem;
		}
	}
}


template <typename T, typename K>
// USAGE WARNING: CALLING THIS CAN INVALIDATE ANY PREVIOUS ELEMENT POINTERS.
// - 'outGot': if non 0, it's set to true if the result is a found element, or false if
//             the result is an added element (it wasn't found).
// - The result can't be 0.
// - If added, the added element is zeroed.
T *HashTable_GetOrAddNoResize(hash_table<T> *table, K key, b32 *outGot = 0){
	Assert(sizeof(((T *)0)->key) == sizeof(*(K *)0));
	Assert(table->mem);
	
	T *directPos = HashTable_KeyToSlot(table, key);
	T *it = directPos;
	T *limit = (T *)table->mem + table->totalSlots;
	while(1){
		if (!it->occupied){
			// Not found: Add.
			ZeroStruct(it);
			it->occupied = 0x1;
			it->key = key;
			table->occupiedSlots++;
			if (outGot) *outGot = false;
			return it;
		}
		if (it->key == key){
			// Found: Get.
			if (outGot) *outGot = true;
			return it;
		}
		it++;
		if (it >= limit){
			if (limit == directPos){
				Assert(table->occupiedSlots == table->totalSlots);
				InvalidCodepath;
				return 0; // Full, not found.
			}
			// Wrap around.
			limit = directPos;
			it = (T *)table->mem;
		}
	}
}

template <typename T, typename K>
// USAGE WARNING: CALLING THIS CAN INVALIDATE ANY PREVIOUS ELEMENT POINTERS.
// - 'outGot': if non 0, it's set to true if the result is a found element, or false if
//             the result is an added element (it wasn't found).
// - The result can't be 0.
// - If added, the added element is zeroed.
T *HashTable_GetOrAdd(hash_table<T> *table, K key, b32 *outGot = 0){
	Assert(sizeof(((T *)0)->key) == sizeof(*(K *)0));
	Assert(table->mem);
	
	if (table->occupiedSlots + 1 > (s32)table->totalSlots*HASHTABLE_MAX_FILLED_FACTOR){
		s32 newTotalSlots = table->totalSlots << 1;
		HashTable_Resize(table, newTotalSlots);
	}

	T *result = HashTable_GetOrAddNoResize(table, key, outGot);
	return result;
}

template <typename T>
// - We could return wether we moved any other elements back if we ever need that.
void HashTable_RemoveNode(hash_table<T> *table, T *node){
	Assert(table->mem);
	Assert(table->occupiedSlots);

	table->occupiedSlots--;
	ZeroStruct(node);

	T *limit = (T *)table->mem + table->totalSlots;
	Assert((u8 *)node >= table->mem && node < limit);

	// Move back the next adjacent elements that are not in their direct pos.
	T *hole = node;
	T *it = node + 1;
	if (it >= limit){
		it = (T *)table->mem;
		limit = node;
	}
	while(it->occupied){
		T *directPosOfIt= HashTable_KeyToSlot(table, it->key);
		if (ThreeCircularIndicesAreAscendingAndFirstTwoCanBeEqual(directPosOfIt, hole, it)){
			*hole = *it; //memcpy(prev, next, sizeof(T));
			ZeroStruct(it);
			hole = it;
		}
		it++;
		if (it >= limit){
			if (it == node){
				InvalidCodepath; // Full.
				return;
			}
			// Wrap around.
			it = (T *)table->mem;
			limit = node;
		}
	}
}

template <typename T>
// USAGE WARNING: CALLING THIS CAN INVALIDATE ANY PREVIOUS ELEMENT POINTERS.
// :YouCanDeleteNodesFromAHashtableWhileIteratingIt
// - 'node' must point to an occupied slot in the hash table.
// - Returns the next occupied element, or 0 if finished. (The "next" will point to the
//   same slot if an element was moved to it because it was closer to its "direct pos")
T *HashTable_RemoveNodeAndGetNext(hash_table<T> *table, T *node){
	HashTable_RemoveNode(table, node);

	if (node->occupied)
		return node;
	return HashTable_NextOccupied(table, node);
}

template <typename T, typename K>
// - Returns true if it removed, false otherwise (key not found).
b32 HashTable_Remove(hash_table<T> *table, K key){
	Assert(sizeof(((T *)0)->key) == sizeof(*(K *)0));
	Assert(table->mem);
	
	T *node = HashTable_Get(table, key);
	if (!node)
		return 0;

	HashTable_RemoveNode(table, node);
	return true;
}

template <typename T>
void HashTable_Clear(hash_table<T> *table){
	Assert(table->mem);
	T *it = (T *)table->mem;
	T *limit = (T *)table->mem + table->totalSlots;
	while(it < limit){
		it->occupied = 0;
		it++;
	}
	table->occupiedSlots = 0;
}

template <typename T>
// USAGE WARNING: CALLING THIS CAN INVALIDATE ALL ELEMENT POINTERS.
b32 HashTable_ResizeIfNeeded(hash_table<T>){
	Assert(table->mem);
	if (table->occupiedSlots > (s32)(table->totalSlots*HASHTABLE_MAX_FILLED_FACTOR)){
		s32 newTotalSlots = table->totalSlots << 1;
		HashTable_Resize(table, newTotalSlots);
	}
}
 
