#pragma once
/// \file page_manager.h
/// The page memory manager and related definitions.

#include <stddef.h>
#include <stdint.h>

#include "kutil/memory.h"
#include "kutil/enum_bitfields.h"

struct page_block;
struct page_table;
struct free_page_header;


/// Manager for allocation of physical pages.
class page_manager
{
public:
	/// Size of a single page.
	static const size_t page_size = 0x1000;

	/// Start of the higher half.
	static const addr_t high_offset = 0xffff800000000000;

	/// Offset from physical where page tables are mapped.
	static const addr_t page_offset = 0xffffff8000000000;

	page_manager();

	/// Allocate and map pages into virtual memory.
	/// \arg address  The virtual address at which to map the pages
	/// \arg count    The number of pages to map
	/// \returns      A pointer to the start of the mapped region
	void * map_pages(addr_t address, size_t count);

	/// Allocate and map contiguous pages into virtual memory, with
	/// a constant offset from their physical address.
	/// \arg count    The number of pages to map
	/// \returns      A pointer to the start of the mapped region, or
	/// nullptr if no region could be found to fit the request.
	void * map_offset_pages(size_t count);

	/// Unmap existing pages from memory.
	/// \arg address  The virtual address of the memory to unmap
	/// \arg count    The number of pages to unmap
	void unmap_pages(void *address, size_t count);

	/// Offset-map a pointer. No physical pages will be mapped.
	/// \arg pointer  Pointer to a pointer to the memory area to be mapped
	/// \arg length   Length of the memory area to be mapped
	void map_offset_pointer(void **pointer, size_t length);

	/// Get the physical address of an offset-mapped pointer
	/// \arg p   Virtual address of memory that has been offset-mapped
	/// \returns Physical address of the memory pointed to by p
	inline addr_t offset_phys(void *p) const
	{
		return reinterpret_cast<addr_t>(kutil::offset_pointer(p, -page_offset));
	}

	/// Get the virtual address of an offset-mapped physical address
	/// \arg a   Physical address of memory that has been offset-mapped
	/// \returns Virtual address of the memory at address a
	inline void * offset_virt(addr_t a) const
	{
		return kutil::offset_pointer(reinterpret_cast<void *>(a), page_offset);
	}

	/// Log the current free/used block lists.
	void dump_blocks();

	/// Get the system page manager.
	/// \returns  A pointer to the system page manager
	static page_manager * get();

private:
	/// Set up the memory manager from bootstraped memory
	void init(
			page_block *free,
			page_block *used,
			page_block *block_cache);

	/// Initialize the virtual memory manager based on this object's state
	void init_memory_manager();

	/// Create a `page_block` struct or pull one from the cache.
	/// \returns  An empty `page_block` struct
	page_block * get_block();

	/// Return a list of `page_block` structs to the cache.
	/// \arg block   A list of `page_block` structs
	void free_blocks(page_block *block);

	/// Allocate a page for a page table, or pull one from the cache
	/// \returns  An empty page mapped in page space
	page_table * get_table_page();

	/// Return a set of mapped contiguous pages to the page cache.
	/// \arg pages  Pointer to the first page to be returned
	/// \arg count  Number of pages in the range
	void free_table_pages(void *pages, size_t count);

	/// Consolidate the free and used block lists. Return freed blocks
	/// to the cache.
	void consolidate_blocks();

	/// Helper to read the PML4 table from CR3.
	/// \returns  A pointer to the current PML4 table.
	static inline page_table * get_pml4()
	{
		addr_t pml4 = 0;
		__asm__ __volatile__ ( "mov %%cr3, %0" : "=r" (pml4) );
		return reinterpret_cast<page_table *>((pml4 & ~0xfffull) + page_offset);
	}

	/// Helper to set the PML4 table pointer in CR3.
	/// \arg pml4  A pointer to the PML4 table to install.
	static inline void set_pml4(page_table *pml4)
	{
		addr_t p = reinterpret_cast<addr_t>(pml4) - page_offset;
		__asm__ __volatile__ ( "mov %0, %%cr3" :: "r" (p & ~0xfffull) );
	}

	/// Helper function to allocate a new page table. If table entry `i` in
	/// table `base` is empty, allocate a new page table and point `base[i]` at
	/// it.
	/// \arg base  Existing page table being indexed into
	/// \arg i     Index into the existing table to check
	void check_needs_page(page_table *base, unsigned i);

	/// Low-level routine for mapping a number of pages into the given page table.
	/// \arg pml4       The root page table to map into
	/// \arg phys_addr  The starting physical address of the pages to be mapped
	/// \arg virt_addr  The starting virtual address ot the memory to be mapped
	/// \arg count      The number of pages to map
	void page_in(
			page_table *pml4,
			addr_t phys_addr,
			addr_t virt_addr,
			size_t count);

	/// Low-level routine for unmapping a number of pages from the given page table.
	/// \arg pml4       The root page table for this mapping
	/// \arg virt_addr  The starting virtual address ot the memory to be unmapped
	/// \arg count      The number of pages to unmap
	void page_out(
			page_table *pml4,
			addr_t virt_addr,
			size_t count);

	/// Get free pages from the free list. Only pages from the first free block
	/// are returned, so the number may be less than requested, but they will
	/// be contiguous. Pages will not be mapped into virtual memory.
	/// \arg count    The maximum number of pages to get
	/// \arg address  [out] The address of the first page
	/// \returns      The number of pages retrieved
	size_t pop_pages(size_t count, addr_t *address);

	page_block *m_free; ///< Free pages list
	page_block *m_used; ///< In-use pages list

	page_block *m_block_cache; ///< Cache of unused page_block structs
	free_page_header *m_page_cache; ///< Cache of free pages to use for tables

	friend void memory_initialize(const void *, size_t, size_t);
	page_manager(const page_manager &) = delete;
};

/// Global page manager.
extern page_manager g_page_manager;

inline page_manager * page_manager::get() { return &g_page_manager; }

/// Flags used by `page_block`.
enum class page_block_flags : uint32_t
{
	free         = 0x00000000,  ///< Not a flag, value for free memory
	used         = 0x00000001,  ///< Memory is in use
	mapped       = 0x00000002,  ///< Memory is mapped to virtual address

	mmio         = 0x00000010,  ///< Memory is a MMIO region
	nonvolatile  = 0x00000020,  ///< Memory is non-volatile storage

	pending_free = 0x10000000,  ///< Memory should be freed
	acpi_wait    = 0x40000000,  ///< Memory should be freed after ACPI init
	permanent    = 0x80000000,  ///< Memory is permanently unusable

	max_flags
};
IS_BITFIELD(page_block_flags);


/// A block of contiguous pages. Each `page_block` represents contiguous
/// physical pages with the same attributes. A `page_block *` is also a
/// linked list of such structures.
struct page_block
{
	addr_t physical_address;
	addr_t virtual_address;
	uint32_t count;
	page_block_flags flags;
	page_block *next;

	inline bool has_flag(page_block_flags f) const { return bitfield_has(flags, f); }
	inline addr_t physical_end() const { return physical_address + (count * page_manager::page_size); }
	inline addr_t virtual_end() const { return virtual_address + (count * page_manager::page_size); }

	inline bool contains(addr_t vaddr) const { return vaddr >= virtual_address && vaddr < virtual_end(); }
	inline bool contains_physical(addr_t addr) const { return addr >= physical_address && addr < physical_end(); }

	/// Helper to zero out a block and optionally set the next pointer.
	/// \arg next  [optional] The value for the `next` pointer
	void zero(page_block *set_next = nullptr);

	/// Helper to copy a bock from another block
	/// \arg other  The block to copy from
	void copy(page_block *other);

	/// \name Page block linked list functions
	/// Functions to act on a `page_block *` as a linked list
	/// @{

	/// Count the items in the given linked list.
	/// \arg list  The list to count
	/// \returns   The number of entries in the list.
	static size_t length(page_block *list);

	/// Append a block or list to the given list.
	/// \arg list   The list to append to
	/// \arg extra  The list or block to be appended 
	/// \returns    The new list head
	static page_block * append(page_block *list, page_block *extra);

	/// Sorted-insert of a block into the list by address.
	/// \arg list   The list to insert into
	/// \arg block  The single block to insert
	/// \returns    The new list head
	static page_block * insert(page_block *list, page_block *block);

	/// Compare two blocks by address.
	/// \arg lhs   The left-hand comparator
	/// \arg rhs   The right-hand comparator
	/// \returns   <0 if lhs is sorts earlier, >0 if lhs sorts later, 0 for equal
	static int compare(const page_block *lhs, const page_block *rhs);

	/// Traverse the list, joining adjacent blocks where possible.
	/// \arg list  The list to consolidate
	/// \returns   A linked list of freed page_block structures.
	static page_block * consolidate(page_block *list);

	/// Traverse the list, printing debug info on this list.
	/// \arg list  The list to print
	/// \arg name  [optional] String to print as the name of this list
	/// \arg show_permanent [optional] If false, hide unmapped blocks
	static void dump(page_block *list, const char *name = nullptr, bool show_unmapped = false);

	/// @}

};



/// Struct to allow easy accessing of a memory page being used as a page table.
struct page_table
{
	using pm = page_manager;

	uint64_t entries[512];

	inline page_table * get(int i) const {
		uint64_t entry = entries[i];
		if ((entry & 0x1) == 0) return nullptr;
		return reinterpret_cast<page_table *>((entry & ~0xfffull) + pm::page_offset);
	}

	inline void set(int i, page_table *p, uint16_t flags) {
		entries[i] = (reinterpret_cast<uint64_t>(p) - pm::page_offset) | (flags & 0xfff);
	}

	void dump(int level = 4, uint64_t offset = page_manager::page_offset);
};


/// Helper struct for computing page table indices of a given address.
struct page_table_indices
{
	page_table_indices(uint64_t v = 0) :
		index{
			(v >> 39) & 0x1ff,
			(v >> 30) & 0x1ff,
			(v >> 21) & 0x1ff,
			(v >> 12) & 0x1ff }
	{}

	/// Get the index for a given level of page table.
	uint64_t & operator[](size_t i) { return index[i]; }
	uint64_t index[4]; ///< Indices for each level of tables.
};


/// Calculate a page-aligned address.
/// \arg p    The address to align.
/// \returns  The next page-aligned address _after_ `p`.
template <typename T> inline T
page_align(T p)
{
	return reinterpret_cast<T>(
		((reinterpret_cast<addr_t>(p) - 1) & ~(page_manager::page_size - 1))
		+ page_manager::page_size);
}

/// Calculate a page-table-aligned address. That is, an address that is
/// page-aligned to the first page in a page table.
/// \arg p    The address to align.
/// \returns  The next page-table-aligned address _after_ `p`.
template <typename T> inline T
page_table_align(T p)
{
	return ((p - 1) & ~0x1fffffull) + 0x200000;
}


/// Calculate the number of pages needed for the give number of bytes.
/// \arg n   Number of bytes
/// \returns Number of pages
inline size_t page_count(size_t n) { return ((n - 1) / page_manager::page_size) + 1; }


/// Bootstrap the memory managers.
void memory_initialize(const void *memory_map, size_t map_length, size_t desc_length);
