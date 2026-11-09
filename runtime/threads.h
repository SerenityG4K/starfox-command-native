/* Threads du NitroSDK sur fibres : interface. Sous licence MIT. */
#pragma once
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

void threads_init(Cpu *main_cpu);
Cpu *rt_current_cpu(void);
Cpu *rt_main_cpu(void);
void hle_os_save_context(Cpu *c);
void hle_os_load_context(Cpu *c);
void hle_irq_thread_switch(Cpu *c);

#ifdef __cplusplus
}
#endif
