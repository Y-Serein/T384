#ifndef T384_COMPILER_H
#define T384_COMPILER_H

#if defined(T384_HOST_SYNTAX_CHECK)
#define T384_FAST_ISR
#define T384_MEMORY_BARRIER() __asm__ volatile("" ::: "memory")
#else
#define T384_FAST_ISR __attribute__((interrupt("WCH-Interrupt-fast")))
#define T384_MEMORY_BARRIER() __asm__ volatile("fence rw, rw" ::: "memory")
#endif

#endif
