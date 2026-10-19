/*
 * The link of a static 64-bit RISC-V program.
 *
 * A static program has no thread-local storage of its own: the kernel
 * gives a static image on this processor no initial TLS block.  The TLS
 * segment is listed so that a link keeping the C library's TLS objects
 * still succeeds (the sysroot's smoke link keeps every object); a program
 * is linked with garbage collection, which leaves the segment empty, and
 * tools/build/check-user-elf.py refuses one that is not.  The small-data
 * sections the compiler may use go with the ordinary data; nothing
 * addresses them through the global pointer, since nothing is linked with
 * relaxation.
 */
/*
 * The link of a static 64-bit RISC-V program.
 *
 * A static program has no thread-local storage of its own: the kernel
 * gives a static image on this processor no initial TLS block, so the
 * link refuses an object that brings one.  The small-data sections the
 * compiler may use go with the ordinary data; nothing addresses them
 * through the global pointer, since nothing is linked with relaxation.
 */
ENTRY(_start)
PHDRS {
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
	tls PT_TLS FLAGS(4);
	stack PT_GNU_STACK FLAGS(6);
}
SECTIONS {
	. = 0x00400000;
	.text : ALIGN(4096) { *(.text .text.*) *(.rodata .rodata.*) *(.srodata .srodata.*) } :text
	. = ALIGN(4096);
	.data : { *(.data .data.*) *(.sdata .sdata.*) } :data
	.got : { *(.got .got.*) } :data
	.tdata : { *(.tdata .tdata.*) } :data :tls
	.tbss : { *(.tbss .tbss.*) } :data :tls
	. = ALIGN(4096);
	.bss : { *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) } :data
	.stack 0 (NOLOAD) : { . += 0x100000; } :stack
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
