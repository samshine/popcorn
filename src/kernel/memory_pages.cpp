#include "assert.h"
#include "console.h"
#include "memory_pages.h"

page_manager g_page_manager;


page_block *
page_block::list_consolidate()
{
	page_block *freed_head = nullptr, **freed = &freed_head;
	for (page_block *cur = this; cur; cur = cur->next) {
		page_block *next = cur->next;

		if (next && cur->flags == next->flags &&
			cur->physical_end() == next->physical_address)
		{
			cur->count += next->count;
			cur->next = next->next;

			next->next = 0;
			*freed = next;
			freed = &next->next;
			continue;
		}
	}

	return freed_head;
}

void
page_block::list_dump(const char *name)
{
	console *cons = console::get();
	cons->puts("Block list");
	if (name) {
		cons->puts(" ");
		cons->puts(name);
	}
	cons->puts(":\n");

	int count = 0;
	for (page_block *cur = this; cur; cur = cur->next) {
		cons->puts("  ");
		cons->put_hex(cur->physical_address);
		cons->puts(" ");
		cons->put_hex((uint32_t)cur->flags);
		if (cur->virtual_address) {
			cons->puts(" ");
			cons->put_hex(cur->virtual_address);
		}
		cons->puts(" [");
		cons->put_dec(cur->count);
		cons->puts("]\n");

		count += 1;
	}

	cons->puts("  Total: ");
	cons->put_dec(count);
	cons->puts("\n");
}


page_manager::page_manager() :
	m_free(nullptr),
	m_used(nullptr),
	m_block_cache(nullptr),
	m_page_cache(nullptr)
{
	kassert(this == &g_page_manager, "Attempt to create another page_manager.");
}

void
page_manager::init(
	page_block *free,
	page_block *used,
	page_block *block_cache,
	uint64_t scratch_start,
	uint64_t scratch_length,
	uint64_t scratch_cur)
{
}