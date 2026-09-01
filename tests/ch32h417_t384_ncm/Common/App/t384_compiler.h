#ifndef T384_COMPILER_H
#define T384_COMPILER_H

#if defined(T384_HOST_SYNTAX_CHECK)
#define T384_FAST_ISR
#else
#define T384_FAST_ISR __attribute__((interrupt("WCH-Interrupt-fast")))
#endif

#endif
