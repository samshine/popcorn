#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "console.h"
#include "guids.h"
#include "utility.h"

size_t ROWS = 0;
size_t COLS = 0;

static EFI_SIMPLE_TEXT_OUT_PROTOCOL *con_out = 0;

const CHAR16 digits[] = {u'0', u'1', u'2', u'3', u'4', u'5', u'6', u'7', u'8', u'9', u'a', u'b', u'c', u'd', u'e', u'f'};

EFI_STATUS
con_pick_mode(EFI_BOOT_SERVICES *bootsvc)
{
	EFI_STATUS status;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx_out_proto;
	status = bootsvc->LocateProtocol(&guid_gfx_out, NULL, (void **)&gfx_out_proto);
	CHECK_EFI_STATUS_OR_RETURN(status, "LocateProtocol gfx");

	const uint32_t modes = gfx_out_proto->Mode->MaxMode;
	uint32_t best = gfx_out_proto->Mode->Mode;

	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info =
		(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *)gfx_out_proto->Mode;

	uint32_t res = info->HorizontalResolution * info->VerticalResolution;
	int is_fb = info->PixelFormat != PixelBltOnly;

	for (uint32_t i = 0; i < modes; ++i) {
		size_t size = 0;
		status = gfx_out_proto->QueryMode(gfx_out_proto, i, &size, &info);
		CHECK_EFI_STATUS_OR_RETURN(status, "QueryMode");

#ifdef MAX_HRES
		if (info->HorizontalResolution > MAX_HRES) continue;
#endif

		const uint32_t new_res = info->HorizontalResolution * info->VerticalResolution;
		const int new_is_fb = info->PixelFormat == PixelBltOnly;

		if (new_is_fb > is_fb && new_res >= res) {
			best = i;
			res = new_res;
		}
	}

	status = gfx_out_proto->SetMode(gfx_out_proto, best);
	CHECK_EFI_STATUS_OR_RETURN(status, "SetMode %d/%d", best, modes);
	return EFI_SUCCESS;
}

EFI_STATUS
con_initialize(EFI_SYSTEM_TABLE *system_table, const CHAR16 *version)
{
	EFI_STATUS status;

	EFI_BOOT_SERVICES *bootsvc = system_table->BootServices;
	con_out = system_table->ConOut;

	// Might not find a video device at all, so ignore not found errors
	status = con_pick_mode(bootsvc);
	if (status != EFI_NOT_FOUND)
		CHECK_EFI_STATUS_OR_RETURN(status, "con_pick_mode");

	status = con_out->QueryMode(con_out, con_out->Mode->Mode, &COLS, &ROWS);
	CHECK_EFI_STATUS_OR_RETURN(status, "QueryMode");

	status = con_out->ClearScreen(con_out);
	CHECK_EFI_STATUS_OR_RETURN(status, "ClearScreen");

	con_out->SetAttribute(con_out, EFI_LIGHTCYAN);
	con_out->OutputString(con_out, (CHAR16 *)L"Popcorn loader ");

	con_out->SetAttribute(con_out, EFI_LIGHTMAGENTA);
	con_out->OutputString(con_out, (CHAR16 *)version);

	con_out->SetAttribute(con_out, EFI_LIGHTGRAY);
	con_out->OutputString(con_out, (CHAR16 *)L" booting...\r\n\n");

	return status;
}

size_t
con_print_hex(uint32_t n)
{
	CHAR16 buffer[9];
	CHAR16 *p = buffer;
	for (int i = 7; i >= 0; --i) {
		uint8_t nibble = (n & (0xf << (i*4))) >> (i*4);
		*p++ = digits[nibble];
	}
	*p = 0;
	con_out->OutputString(con_out, buffer);
	return 8;
}

size_t
con_print_long_hex(uint64_t n)
{
	CHAR16 buffer[17];
	CHAR16 *p = buffer;
	for (int i = 15; i >= 0; --i) {
		uint8_t nibble = (n & (0xf << (i*4))) >> (i*4);
		*p++ = digits[nibble];
	}
	*p = 0;
	con_out->OutputString(con_out, buffer);
	return 16;
}

size_t
con_print_dec(uint32_t n)
{
	CHAR16 buffer[11];
	CHAR16 *p = buffer + 10;
	*p-- = 0;
	do {
		*p-- = digits[n % 10];
		n /= 10;
	} while (n != 0);

	con_out->OutputString(con_out, ++p);
	return 10 - (p - buffer);
}

size_t
con_print_long_dec(uint64_t n)
{
	CHAR16 buffer[21];
	CHAR16 *p = buffer + 20;
	*p-- = 0;
	do {
		*p-- = digits[n % 10];
		n /= 10;
	} while (n != 0);

	con_out->OutputString(con_out, ++p);
	return 20 - (p - buffer);
}

size_t
con_printf(const CHAR16 *fmt, ...)
{
	CHAR16 buffer[256];
	const CHAR16 *r = fmt;
	CHAR16 *w = buffer;
	va_list args;
	size_t count = 0;

	va_start(args, fmt);

	while (r && *r) {
		if (*r != L'%') {
			count++;
			*w++ = *r++;
			continue;
		}

		*w = 0;
		con_out->OutputString(con_out, buffer);
		w = buffer;

		r++; // chomp the %

		switch (*r++) {
			case L'%':
				con_out->OutputString(con_out, L"%");
				count++;
				break;

			case L'x':
				count += con_print_hex(va_arg(args, uint32_t));
				break;

			case L'd':
			case L'u':
				count += con_print_dec(va_arg(args, uint32_t));
				break;

			case L's':
				{
					CHAR16 *s = va_arg(args, CHAR16*);
					count += wstrlen(s);
					con_out->OutputString(con_out, s);
				}
				break;

			case L'l':
				switch (*r++) {
					case L'x':
						count += con_print_long_hex(va_arg(args, uint64_t));
						break;

					case L'd':
					case L'u':
						count += con_print_long_dec(va_arg(args, uint64_t));
						break;

					default:
						break;
				}
				break;

			default:
				break;
		}
	}

	*w = 0;
	con_out->OutputString(con_out, buffer);

	va_end(args);
	return count;
}

void
con_status_begin(const CHAR16 *message)
{
	con_out->SetAttribute(con_out, EFI_LIGHTGRAY);
	con_out->OutputString(con_out, (CHAR16 *)message);
}

void
con_status_ok()
{
	con_out->SetAttribute(con_out, EFI_LIGHTGRAY);
	con_out->OutputString(con_out, (CHAR16 *)L"[");
	con_out->SetAttribute(con_out, EFI_GREEN);
	con_out->OutputString(con_out, (CHAR16 *)L"  ok  ");
	con_out->SetAttribute(con_out, EFI_LIGHTGRAY);
	con_out->OutputString(con_out, (CHAR16 *)L"]\r\n");
}

void
con_status_fail(const CHAR16 *error)
{
	con_out->SetAttribute(con_out, EFI_LIGHTGRAY);
	con_out->OutputString(con_out, (CHAR16 *)L"[");
	con_out->SetAttribute(con_out, EFI_LIGHTRED);
	con_out->OutputString(con_out, (CHAR16 *)L"failed");
	con_out->SetAttribute(con_out, EFI_LIGHTGRAY);
	con_out->OutputString(con_out, (CHAR16 *)L"]\r\n");

	con_out->SetAttribute(con_out, EFI_RED);
	con_out->OutputString(con_out, (CHAR16 *)error);
	con_out->SetAttribute(con_out, EFI_LIGHTGRAY);
	con_out->OutputString(con_out, (CHAR16 *)L"\r\n");
}

EFI_STATUS
con_get_framebuffer(
	EFI_BOOT_SERVICES *bootsvc,
	void **buffer,
	size_t *buffer_size,
	uint32_t *hres,
	uint32_t *vres,
	uint32_t *rmask,
	uint32_t *gmask,
	uint32_t *bmask)
{
	EFI_STATUS status;

	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	status = bootsvc->LocateProtocol(&guid_gfx_out, NULL, (void **)&gop);
	if (status != EFI_NOT_FOUND) {
		CHECK_EFI_STATUS_OR_RETURN(status, "LocateProtocol gfx");

		*buffer = (void *)gop->Mode->FrameBufferBase;
		*buffer_size = gop->Mode->FrameBufferSize;
		*hres = gop->Mode->Info->HorizontalResolution;
		*vres = gop->Mode->Info->VerticalResolution;

		switch (gop->Mode->Info->PixelFormat) {
			case PixelRedGreenBlueReserved8BitPerColor:
				*rmask = 0x0000ff;
				*gmask = 0x00ff00;
				*bmask = 0xff0000;
				return EFI_SUCCESS;

			case PixelBlueGreenRedReserved8BitPerColor:
				*bmask = 0x0000ff;
				*gmask = 0x00ff00;
				*rmask = 0xff0000;
				return EFI_SUCCESS;

			case PixelBitMask:
				*rmask = gop->Mode->Info->PixelInformation.RedMask;
				*gmask = gop->Mode->Info->PixelInformation.GreenMask;
				*bmask = gop->Mode->Info->PixelInformation.BlueMask;
				return EFI_SUCCESS;

			default:
				// Not a framebuffer, fall through to zeroing out
				// values below.
				break;
		}
	}

	*buffer = NULL;
	*buffer_size = *hres = *vres = 0;
	*rmask = *gmask = *bmask = 0;
	return EFI_SUCCESS;
}
