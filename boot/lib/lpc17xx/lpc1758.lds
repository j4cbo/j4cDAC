/* j4cDAC linker script */

OUTPUT_FORMAT ("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
ENTRY(_rom_reset)
SEARCH_DIR(.)

MEMORY {
	rom (rx): ORIGIN = 0x00000000, LENGTH = 16K
	ram (rwx): ORIGIN = 0x10000000, LENGTH = 32K
	ahb_sram_0 (rwx): ORIGIN = 0x2007C000, LENGTH = 32K
}

/* These force the linker to search for particular symbols from
 * the start of the link process and thus ensure the user's
 * overrides are picked up
 */

SECTIONS {

	_rom_reset = _reset - 0x10000000;

	.text : {
		CREATE_OBJECT_SYMBOLS

		__interrupt_vector_start = .;
		KEEP(*(.interrupt_vector));

		/* Make sure we pulled in an interrupt vector.	*/
		ASSERT (. != __interrupt_vector_start, "No interrupt vector");

		*(.rom)
		*(.rom.b)

		*(.text)
		*(.text.*)

		*(.reset)

		/* Make sure we pulled in some reset code.	*/
		ASSERT (. != _reset, "No reset code");

		*(.text .text.* .gnu.linkonce.t.*)
		*(.rodata .rodata.* .gnu.linkonce.r.*)

		. = ALIGN(4);
		KEEP(*(.init))
	} >ram AT >rom

	.data : {
		*(.cs3.region-head.ram)
		KEEP(*(.jcr))
		*(.got.plt) *(.got)
		*(.shdata)
		*(.data .data.* .gnu.linkonce.d.*)
		*(.ram)
		. = ALIGN (8);
		_edata = .;

		_copy_end = .;

	} >ram AT>rom

	.bss :
	{
		_bss_start = .;
		*(.shbss)
		*(.bss SORT_BY_ALIGNMENT(.bss.*) .gnu.linkonce.b.*)
		*(COMMON)
		*(.ram.b)
		. = ALIGN (8);
		_end = .;
	} >ram AT>rom

	.stack :
	{
		*(.stack)
	} >ram

	.ahb_sram_0 (NOLOAD) :
	{
		*(.ahb_sram_0)
	} > ahb_sram_0

	.note.gnu.arm.ident 0 : { KEEP (*(.note.gnu.arm.ident)) }
	.ARM.attributes 0 : { KEEP (*(.ARM.attributes)) }
	/DISCARD/ : { *(.note.GNU-stack) }
}
