#ifndef T384_TEST_FLASH_H
#define T384_TEST_FLASH_H
#include <stdint.h>
#define FLASH_BASE 0x08000000u
#define FLASH_CFGR0_BASE 0x10000000u
typedef enum { FLASH_COMPLETE, FLASH_ERROR_PG } FLASH_Status;
void FLASH_Unlock(void);
void FLASH_Lock(void);
FLASH_Status FLASH_ErasePage(uint32_t address);
FLASH_Status FLASH_ProgramWord(uint32_t address, uint32_t word);
#endif
