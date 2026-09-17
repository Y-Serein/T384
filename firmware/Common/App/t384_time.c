#include "t384_time.h"

#include "ch32h417.h"
#include "system_ch32h417.h"
#include "t384_compiler.h"

#if T384_DUALCORE
#include "t384_dualcore.h"
#endif

#if !T384_DUALCORE || defined(Core_V3F)
static volatile uint32_t milliseconds;
#endif

void t384_time_init(void)
{
#if T384_DUALCORE && defined(Core_V5F)
    /* V3F alone owns SysTick0; both cores use its shared monotonic clock. */
    return;
#else
    milliseconds = 0u;
    SysTick0->CTLR = 0u;
    SysTick0->ISR &= ~(1u << 0);
    SysTick0->CNT = 0u;
    SysTick0->CMP = (HCLKClock / 1000u) - 1u;
    NVIC_SetPriority(SysTick0_IRQn, 0x80u);
    NVIC_EnableIRQ(SysTick0_IRQn);
    /* enable, HCLK source, interrupt, automatic reload */
    SysTick0->CTLR = 0x0fu;
#endif
}

uint32_t t384_millis(void)
{
#if T384_DUALCORE && defined(Core_V5F)
    return __atomic_load_n(&t384_dualcore_shared.clock_ms, __ATOMIC_ACQUIRE);
#else
    return milliseconds;
#endif
}

uint32_t sys_now(void)
{
    return t384_millis();
}

#if !T384_DUALCORE || defined(Core_V3F)
void SysTick0_Handler(void) T384_FAST_ISR;
void SysTick0_Handler(void)
{
    SysTick0->ISR &= ~(1u << 0);
    ++milliseconds;
#if T384_DUALCORE
    __atomic_store_n(&t384_dualcore_shared.clock_ms, milliseconds, __ATOMIC_RELEASE);
#endif
}
#endif
