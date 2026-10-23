/*
 * The link of a static 32-bit ARM program.
 *
 * A static program has no thread-local storage of its own: the kernel
 * gives a static image on this processor no initial TLS block.  The TLS
 * segment is listed so that a link keeping the C library's TLS objects
 * still succeeds (the sysroot's smoke link keeps every object); a program
 * is linked with garbage collection, which leaves the segment empty.  The
 * unwind index is discarded: nothing on this port unwinds through it yet.
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
	.text : ALIGN(4096) { *(.text .text.*) *(.rodata .rodata.*) } :text
	. = ALIGN(4096);
	.data : { *(.data .data.*) } :data
	.got : { *(.got .got.*) } :data
	.tdata : { *(.tdata .tdata.*) } :data :tls
	.tbss : { *(.tbss .tbss.*) } :data :tls
	. = ALIGN(4096);
	.bss : { *(.bss .bss.*) *(COMMON) } :data
	.stack 0 (NOLOAD) : { . += 0x100000; } :stack
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.exidx*) *(.ARM.extab*) }
}
