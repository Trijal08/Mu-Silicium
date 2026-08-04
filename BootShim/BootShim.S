#define LINUX_IMAGE_FLAGS      ((1 << 1) | (1 << 3))

_Head:
  adr   x1, _Payload    /* Set _Entry address */
  b     _Start          /* Jump to the real code */
#if REQUIRES_KERNEL_HEADER == 1
  .quad 0                  /* Text Offset (purely cosmetic) */
  .quad FD_SIZE            /* Image Size */
  .quad LINUX_IMAGE_FLAGS  /* Flags */
  .quad 0                  /* Reserved */
  .quad 0                  /* Reserved */
  .quad 0                  /* Reserved */
  .ascii "ARM\x64"         /* ARM64 Magic */
  .long 0                  /* Reserved */
#endif

_StackBase:
  .quad FD_BASE         /* Text Offset */

_StackSize:
  .quad FD_SIZE            /* Image Size */

_Start:
  mov   x4, x1
  ldr   x5, _StackBase
  cmp   x4, x5
  beq   _Entry
  ldr   x6,  _StackSize

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop
  ldr   x5, _StackBase

_Entry:
  br    x5

_Dead:
  b     _Dead

.text
.align 4

_Payload:
