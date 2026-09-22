ENTRY( _start )

__stack_size = 2048;

PROVIDE( _stack_size = __stack_size );


MEMORY
{
	/*FLASH, RAM, and RAM-CODE can be freely configured according to project 
	requirements;Please refer to the datasheet manual for specific allocation*/
	FLASH (rx) : ORIGIN = 0x00030000, LENGTH = 128K             /* zero_wait flah ,and loaded into RAM_CODE for running */
	RAM_CODE (xrw) : ORIGIN = 0x200A0000, LENGTH = 128K         /* V5F network/data-plane code */
	RAM (xrw) : ORIGIN = 0x200F6300, LENGTH = (19K+256)  /*DTCM RAM for .data .bss section and other*/ 
   	FRAME (xrw) : ORIGIN = 0x200C0300, LENGTH = 216K
    DMA_SHARED (xrw) : ORIGIN = 0x2017C000, LENGTH = 12K
    /* The old ITCM window is consumed by the enlarged V5F code bank.  The
     * metadata symbol is placed at the head of FRAME1_DTCM below. */
    FRAME1_ITCM (xrw) : ORIGIN = 0x200A8000, LENGTH = 1K
    FRAME1_DTCM (xrw) : ORIGIN = 0x200FB000, LENGTH = 18K
    FRAME1_CODE (xrw) : ORIGIN = 0x20125800, LENGTH = 42K
    FRAME1_DATA (xrw) : ORIGIN = 0x2016D000, LENGTH = 60K
    STACK_RAM (xrw) : ORIGIN = 0x200FF800, LENGTH = 2K
    IPC (xrw) : ORIGIN = 0x2017F000, LENGTH = 1K
    RAM_LOAD (xrw) : ORIGIN = (0x200C0000+512), LENGTH = 256           /* RAM for .load section*/
}

SECTIONS
{

	.init :
	{
		_sinit = .;
		. = ALIGN(4);
		KEEP(*(SORT_NONE(.init)))
		. = ALIGN(4);
		_einit = .;
	} >FLASH AT>FLASH

    .loadcodelalign : 
    {       
        . = ALIGN(4);
        PROVIDE(_loadcode_lma = .); 
    } >FLASH AT>FLASH 

	.loadcode :
	{
		. = ALIGN(4);
        PROVIDE(_loadcode_vma_start = .);
		*(.load);
		. = ALIGN(4); 
		PROVIDE(_loadcode_vma_end = .);
	}>RAM_LOAD AT>FLASH

    .highcodelalign : 
    {       
        . = ALIGN(4);
        PROVIDE(_highcode_lma = .); 
    } >FLASH AT>FLASH 
    
    .highcode : 
    {
        . = ALIGN(4);
        PROVIDE(_highcode_vma_start = .);
        *(.vector);
		KEEP(*(SORT_NONE(.vector_handler)))
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata*)
        *(.sdata2.*)
		*(.glue_7)
		*(.glue_7t)
		*(.gnu.linkonce.t.*)
		. = ALIGN(4); 
        PROVIDE(_highcode_vma_end = .);
    } >RAM_CODE AT>FLASH

	.text :
	{
		. = ALIGN(4);
		KEEP(*(SORT_NONE(.handle_reset)))
		. = ALIGN(4);
	} >FLASH AT>FLASH 

    /* V5F-only compressed Web console. It is intentionally executed/read
     * directly from zero-wait Flash and is not copied into the 128 KiB
     * RAM_CODE window. */
    .t384_http_rodata :
    {
        . = ALIGN(4);
        KEEP(*(.t384_http_rodata))
        . = ALIGN(4);
    } >FLASH AT>FLASH

    .highcodelalign1 : 
    {       
        . = ALIGN(4);
        PROVIDE(_highcode_lma1 = .); 
    } >FLASH AT>FLASH 

	.fini :
	{
	    . = ALIGN(4);
        PROVIDE(_highcode_vma_start1 = .);
		KEEP(*(SORT_NONE(.fini)))
		. = ALIGN(4);
	} >RAM_CODE AT>FLASH

	PROVIDE( _etext = . );
	PROVIDE( _eitcm = . );	

	.preinit_array  :
	{
	  PROVIDE_HIDDEN (__preinit_array_start = .);
	  KEEP (*(.preinit_array))
	  PROVIDE_HIDDEN (__preinit_array_end = .);
	} >RAM_CODE AT>FLASH 
	
	.init_array     :
	{
	  PROVIDE_HIDDEN (__init_array_start = .);
	  KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
	  KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
	  PROVIDE_HIDDEN (__init_array_end = .);
	} >RAM_CODE AT>FLASH 
	
	.fini_array     :
	{
	  PROVIDE_HIDDEN (__fini_array_start = .);
	  KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
	  KEEP (*(.fini_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .dtors))
	  PROVIDE_HIDDEN (__fini_array_end = .);
	} >RAM_CODE AT>FLASH 
	
	.ctors          :
	{
	  /* gcc uses crtbegin.o to find the start of
	     the constructors, so we make sure it is
	     first.  Because this is a wildcard, it
	     doesn't matter if the user does not
	     actually link against crtbegin.o; the
	     linker won't look for a file to match a
	     wildcard.  The wildcard also means that it
	     doesn't matter which directory crtbegin.o
	     is in.  */
	  KEEP (*crtbegin.o(.ctors))
	  KEEP (*crtbegin?.o(.ctors))
	  /* We don't want to include the .ctor section from
	     the crtend.o file until after the sorted ctors.
	     The .ctor section from the crtend file contains the
	     end of ctors marker and it must be last */
	  KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
	  KEEP (*(SORT(.ctors.*)))
	  KEEP (*(.ctors))
	} >RAM_CODE AT>FLASH 
	
	.dtors          :
	{
	  KEEP (*crtbegin.o(.dtors))
	  KEEP (*crtbegin?.o(.dtors))
	  KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .dtors))
	  KEEP (*(SORT(.dtors.*)))
	  KEEP (*(.dtors))
	  . = ALIGN(4); 
      PROVIDE(_highcode_vma_end1 = .);
	} >RAM_CODE AT>FLASH 

	.dalign :
	{
		. = ALIGN(4);
		PROVIDE(_data_vma = .);
	} >RAM AT>FLASH	

	.dlalign :
	{
		. = ALIGN(4); 
		PROVIDE(_data_lma = .);
	} >FLASH AT>FLASH

	.data :
	{
		. = ALIGN(4);
    	*(.gnu.linkonce.r.*)
    	*(.data .data.*)
    	*(.gnu.linkonce.d.*)
		. = ALIGN(8);
    	PROVIDE( __global_pointer$ = . + 0x800 );
    	*(.sdata .sdata.*)
		*(.sdata2.*)
    	*(.gnu.linkonce.s.*)
    	. = ALIGN(8);
    	*(.srodata.cst16)
    	*(.srodata.cst8)
    	*(.srodata.cst4)
    	*(.srodata.cst2)
    	*(.srodata .srodata.*)
    	. = ALIGN(4);
		PROVIDE( _edata = .);
	} >RAM AT>FLASH

	.bss :
	{
		. = ALIGN(4);
		PROVIDE( _sbss = .);
  	    *(.sbss*)
        *(.gnu.linkonce.sb.*)
		*(.bss*)
     	*(.gnu.linkonce.b.*)		
		*(COMMON*)
		. = ALIGN(4);
		PROVIDE( _ebss = .);
	} >RAM AT>FLASH

	PROVIDE( _end = _ebss);
	PROVIDE( end = . );

    .stack ORIGIN(STACK_RAM) :
    {
        PROVIDE( _heap_end = ORIGIN(RAM) + LENGTH(RAM) );    
        . = ALIGN(4);
        PROVIDE(_susrstack = . );
        . = . + __stack_size;
        PROVIDE( _eusrstack = .);
    } >STACK_RAM 


    .t384_frame (NOLOAD) : { . = ALIGN(32); KEEP(*(.t384_frame)) } >FRAME
    .t384_frame1_itcm (NOLOAD) : { . = ALIGN(32); KEEP(*(.t384_frame1_itcm)) } >FRAME1_DTCM
    .t384_net_http (NOLOAD) : { . = ALIGN(32); KEEP(*(.t384_net_http*)) } >FRAME1_DTCM
    .t384_frame1_dtcm (NOLOAD) : { . = ALIGN(32); KEEP(*(.t384_frame1_dtcm)) } >FRAME1_DTCM
    .t384_net_ncm (NOLOAD) : { . = ALIGN(32); KEEP(*(.t384_net_ncm*)) } >FRAME1_CODE
    .t384_frame1_code (NOLOAD) : { . = ALIGN(32); KEEP(*(.t384_frame1_code)) } >FRAME1_CODE
    .t384_net_heap (NOLOAD) : { . = ALIGN(32); KEEP(*(.t384_net_heap*)) } >FRAME1_DATA
    .t384_frame1_data (NOLOAD) : { . = ALIGN(32); KEEP(*(.t384_frame1_data)) } >FRAME1_DATA
    .t384_dma (NOLOAD) : { . = ALIGN(32); KEEP(*(.t384_dma)) } >DMA_SHARED
    .t384_ipc (NOLOAD) : { . = ALIGN(32); KEEP(*(.t384_ipc)) } >IPC
    /* 640 packed metadata is 1056 B; 384 non-packed metadata is 384 B.
     * Both profiles share this V5F network linker layout. */
    ASSERT(SIZEOF(.t384_frame1_itcm) == 1056 ||
           SIZEOF(.t384_frame1_itcm) == 384, "V5F network metadata size")
    ASSERT(SIZEOF(.t384_frame1_dtcm) == 18432 || SIZEOF(.t384_frame1_dtcm) == 32 || SIZEOF(.t384_frame1_dtcm) == 18144, "Frame1 DTCM size")
    ASSERT(SIZEOF(.t384_frame1_code) == 43008 || SIZEOF(.t384_frame1_code) == 32 || SIZEOF(.t384_frame1_code) == 41472, "Frame1 shared code size")
    ASSERT(SIZEOF(.t384_frame1_data) == 61440 || SIZEOF(.t384_frame1_data) == 32 || SIZEOF(.t384_frame1_data) == 56960, "Frame1 shared data size")
    ASSERT(SIZEOF(.t384_frame1_itcm) + SIZEOF(.t384_net_http) + SIZEOF(.t384_frame1_dtcm) <= LENGTH(FRAME1_DTCM), "V5F HTTP storage exceeds DTCM bank")
    ASSERT(SIZEOF(.t384_net_ncm) <= LENGTH(FRAME1_CODE), "V5F NCM storage exceeds shared-code bank")
    ASSERT(SIZEOF(.t384_net_heap) <= LENGTH(FRAME1_DATA), "V5F lwIP storage exceeds shared-data bank")
    ASSERT(_start == 0x30000, "V5F wake address must match entry")
    /* 640 packed ring: 165888 B; 384 non-packed ring: 147456 B. */
    ASSERT(SIZEOF(.t384_frame) == 165888 ||
           SIZEOF(.t384_frame) == 147456,
           "V5F network capture buffer size")
    ASSERT(SIZEOF(.t384_ipc) <= 1024, "IPC exceeds 1 KiB")
    ASSERT(_ebss + 8192 <= _heap_end, "V5F needs 8 KiB heap margin")
}
