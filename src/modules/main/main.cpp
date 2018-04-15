#include <stddef.h>
#include <stdint.h>

#include "console.h"
#include "font.h"
#include "kernel_data.h"
#include "screen.h"

extern "C" {
	void do_the_set_registers(popcorn_data *header);
	void kernel_main(popcorn_data *header);
}

console
load_console(const popcorn_data *header)
{
	return console{
		font::load(header->font),
		screen{
			header->frame_buffer,
			header->hres,
			header->vres,
			header->rmask,
			header->gmask,
			header->bmask},
		header->log,
		header->log_length};
}

void
kernel_main(popcorn_data *header)
{
	console cons = load_console(header);

	cons.set_color(0x21, 0x00);
	cons.puts("Popcorn OS ");
	cons.set_color(0x08, 0x00);
	cons.puts(GIT_VERSION " booting...\n");

	do_the_set_registers(header);
}