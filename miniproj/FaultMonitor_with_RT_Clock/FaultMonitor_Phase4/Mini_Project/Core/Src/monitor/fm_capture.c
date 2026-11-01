/*
 * fm_capture.c -- see fm_capture.h. Owner: Abhay.
 *
 * Register references: RM0390 (STM32F446 reference manual) section 6.3.21
 * for RCC_CSR; PM0214 (Cortex-M4 programming manual) sections 4.2 (MPU)
 * and 4.4.14-4.4.19 (CFSR, HFSR, MMFAR, BFAR).
 */
#include "fm_capture.h"
#include "fm_trace.h"

#include "main.h"          /* CMSIS: SCB, MPU, RCC, NVIC, __get_PSP */
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

#if FM_ENABLE_MONITOR

/* Lives in the .noinit section added to STM32F446RETX_FLASH.ld, so the
 * C startup code does not zero it and its content survives a reset. */
fm_noinit_t g_fm_noinit __attribute__((section(".noinit")));

static uint16_t s_reset_cause;

/* ---- Boot: reset cause ---------------------------------------------- */

void fm_capture_boot(void)
{
    uint32_t csr = RCC->CSR;
    uint16_t f = 0;
    if (csr & RCC_CSR_IWDGRSTF) f |= FM_RST_IWDG;
    if (csr & RCC_CSR_WWDGRSTF) f |= FM_RST_WWDG;
    if (csr & RCC_CSR_SFTRSTF)  f |= FM_RST_SW;
    if (csr & RCC_CSR_LPWRRSTF) f |= FM_RST_LPWR;
    if (csr & RCC_CSR_PORRSTF)  f |= FM_RST_POR;
    if (csr & RCC_CSR_BORRSTF)  f |= FM_RST_BOR;
    if (csr & RCC_CSR_PINRSTF)  f |= FM_RST_PIN;
    s_reset_cause = f;
    RCC->CSR |= RCC_CSR_RMVF;          /* clear, so the next boot is fresh */

    /* After power-on the RAM content is random: start clean. */
    if ((f & (FM_RST_POR | FM_RST_BOR)) || g_fm_noinit.magic != FM_NOINIT_MAGIC) {
        memset(&g_fm_noinit, 0, sizeof(g_fm_noinit));
        g_fm_noinit.magic    = FM_NOINIT_MAGIC;
        g_fm_noinit.cur_comp = FM_COMP_OTHER;
    }
    g_fm_noinit.boot_count++;
}

uint16_t fm_reset_cause(void)
{
    return s_reset_cause;
}

const char *fm_reset_cause_str(uint16_t f)
{
    /* Every reset also pulses the NRST pin, so PIN is reported only when
     * nothing more specific is set. */
    if (f & FM_RST_IWDG) return "IWDG watchdog";
    if (f & FM_RST_WWDG) return "WWDG watchdog";
    if (f & FM_RST_SW)   return "software reset";
    if (f & FM_RST_LPWR) return "low-power";
    if (f & FM_RST_BOR)  return "brown-out";
    if (f & FM_RST_POR)  return "power-on";
    if (f & FM_RST_PIN)  return "reset button";
    return "unknown";
}

/* ---- MPU ------------------------------------------------------------- */

#define MPU_REGION_NULL      0U
#define MPU_REGION_RO        1U
#define MPU_REGION_GUARD0    2U
#define MPU_NUM_GUARDS       6U    /* regions 2..7 */
#define GUARD_SIZE           32U
#define GUARD_SIZE_FIELD     4U    /* RASR.SIZE: 2^(4+1) = 32 bytes */

static uint8_t  s_guard_comp[MPU_NUM_GUARDS];
static uint32_t s_guard_base[MPU_NUM_GUARDS];
static uint32_t s_stack_base[FM_MAX_COMP];   /* pxStackBase per task comp */
static bool     s_ro_used;

/* RASR helpers. AP: 000 no access, 110 read-only (privileged and user). */
#define RASR(ap, size_field, xn)                                   \
    (((uint32_t)(xn) << MPU_RASR_XN_Pos) |                         \
     ((uint32_t)(ap) << MPU_RASR_AP_Pos) |                         \
     (1UL << MPU_RASR_S_Pos) | (1UL << MPU_RASR_C_Pos) |           \
     ((uint32_t)(size_field) << MPU_RASR_SIZE_Pos) | MPU_RASR_ENABLE_Msk)

static void mpu_set(uint32_t region, uint32_t base, uint32_t rasr)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    MPU->RNR  = region;
    MPU->RBAR = base & MPU_RBAR_ADDR_Msk;
    MPU->RASR = rasr;
    __DSB();
    __ISB();
    __set_PRIMASK(pm);
}

static uint32_t size_field(uint32_t size)
{
    /* size = 2^(field+1) */
    uint32_t f = 0;
    while ((2UL << f) < size) {
        f++;
    }
    return f;
}

void fm_mpu_init(void)
{
    memset(s_guard_comp, FM_COMP_NONE, sizeof(s_guard_comp));
    memset(s_stack_base, 0, sizeof(s_stack_base));
    s_ro_used = false;

    /* The vector table is at 0x08000000 and, by default, also visible at
     * address 0 (boot alias). Point VTOR at the flash copy so the null
     * guard at address 0 can never block an exception entry. */
    SCB->VTOR = FLASH_BASE;
    __DSB();

    MPU->CTRL = 0;
    for (uint32_t r = 0; r < 8U; r++) {
        MPU->RNR  = r;
        MPU->RASR = 0;
    }

    /* Region 0: 1 KB no-access at 0x00000000 -> any NULL pointer access
     * (including ptr->field up to 1 KB) raises MemManage. */
    MPU->RNR  = MPU_REGION_NULL;
    MPU->RBAR = 0x00000000UL;
    MPU->RASR = RASR(0U, size_field(1024U), 1U);

    /* PRIVDEFENA: everything not covered by a region keeps the default
     * memory map, so only our regions change behaviour. */
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;

    /* Route faults to their own handlers instead of escalating straight
     * to HardFault, so the handler knows what kind of fault it was. */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
                  SCB_SHCSR_USGFAULTENA_Msk;
    /* Divide-by-zero becomes a UsageFault instead of silently returning 0. */
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
    __DSB();
    __ISB();
}

bool fm_mpu_protect_ro(const void *addr, uint32_t size)
{
    uint32_t a = (uint32_t)addr;
    if (s_ro_used || size < 32U || (size & (size - 1U)) || (a & (size - 1U))) {
        return false;
    }
    mpu_set(MPU_REGION_RO, a, RASR(6U, size_field(size), 1U));
    s_ro_used = true;
    return true;
}

void fm_mpu_guard_task(uint8_t comp_id, void *task_handle)
{
    if (comp_id >= FM_MAX_COMP || task_handle == NULL) {
        return;
    }
    TaskStatus_t st;
    vTaskGetInfo((TaskHandle_t)task_handle, &st, pdFALSE, eInvalid);
    uint32_t stack_base = (uint32_t)st.pxStackBase;
    s_stack_base[comp_id] = stack_base;

    /* Find this component's slot, or a free one. */
    int slot = -1;
    for (uint32_t i = 0; i < MPU_NUM_GUARDS; i++) {
        if (s_guard_comp[i] == comp_id) { slot = (int)i; break; }
    }
    if (slot < 0) {
        for (uint32_t i = 0; i < MPU_NUM_GUARDS; i++) {
            if (s_guard_comp[i] == FM_COMP_NONE) { slot = (int)i; break; }
        }
    }
    if (slot < 0) {
        return;   /* out of MPU regions: this task gets no guard */
    }

    /* Skip the lowest 16 bytes (FreeRTOS's own overflow check reads
     * them) and round up to a 32-byte boundary, as the MPU requires. */
    uint32_t guard = (stack_base + 16U + (GUARD_SIZE - 1U)) & ~(GUARD_SIZE - 1U);
    s_guard_comp[slot] = comp_id;
    s_guard_base[slot] = guard;
    mpu_set(MPU_REGION_GUARD0 + (uint32_t)slot, guard, RASR(0U, GUARD_SIZE_FIELD, 1U));
}

static int guard_slot_of(uint8_t comp_id)
{
    for (uint32_t i = 0; i < MPU_NUM_GUARDS; i++) {
        if (s_guard_comp[i] == comp_id) return (int)i;
    }
    return -1;
}

void fm_mpu_unguard_task(uint8_t comp_id)
{
    int slot = guard_slot_of(comp_id);
    if (slot < 0) {
        return;
    }
    mpu_set(MPU_REGION_GUARD0 + (uint32_t)slot, 0, 0);   /* RASR 0 = region off */
    s_guard_comp[slot] = FM_COMP_NONE;
    s_stack_base[comp_id] = 0;
}

uint32_t fm_stack_free_words(uint8_t comp_id)
{
    if (comp_id >= FM_MAX_COMP || s_stack_base[comp_id] == 0U) {
        return 0xFFFFU;
    }
    /* FreeRTOS fills new stacks with 0xA5. Count untouched words from just
     * above the guard (reading the guard itself would trap). */
    int slot = guard_slot_of(comp_id);
    uint32_t start = (slot >= 0) ? s_guard_base[slot] + GUARD_SIZE
                                 : s_stack_base[comp_id] + 16U;
    const volatile uint32_t *p = (const volatile uint32_t *)start;
    uint32_t n = 0;
    while (n < 4096U && p[n] == 0xA5A5A5A5UL) {
        n++;
    }
    return n;
}

uint32_t fm_task_pc(void *task_handle)
{
    if (task_handle == NULL || task_handle == xTaskGetCurrentTaskHandle()) {
        return 0;
    }
    TaskStatus_t st;
    vTaskGetInfo((TaskHandle_t)task_handle, &st, pdFALSE, eInvalid);
    uint32_t lo = (uint32_t)st.pxStackBase;
    /* pxTopOfStack is the first member of the TCB. The ARM_CM4F port
     * saves r4-r11 and EXC_RETURN there, then s16-s31 if the task used
     * the FPU (EXC_RETURN bit 4 clear), then the hardware frame
     * r0 r1 r2 r3 r12 lr pc xpsr. */
    const uint32_t *top = *(const uint32_t *const *)task_handle;
    uint32_t t = (uint32_t)top;
    if (t < lo || (t & 3U) || t > lo + 0x10000U) {
        return 0;
    }
    uint32_t exc_return = top[8];
    uint32_t idx = 9U + (((exc_return & 0x10U) == 0U) ? 16U : 0U) + 6U;
    return top[idx];
}

/* ---- Fault traps ------------------------------------------------------ */

static bool addr_in_guard(uint32_t addr)
{
    for (uint32_t i = 0; i < MPU_NUM_GUARDS; i++) {
        if (s_guard_comp[i] != FM_COMP_NONE &&
            addr >= s_guard_base[i] && addr < s_guard_base[i] + GUARD_SIZE) {
            return true;
        }
    }
    return false;
}

static void finish_crash_record(uint8_t type, uint8_t comp) __attribute__((noreturn));
static void finish_crash_record(uint8_t type, uint8_t comp)
{
    g_fm_noinit.fault_type = type;
    g_fm_noinit.comp_id    = comp;
    g_fm_noinit.cycle_ts   = DWT->CYCCNT;
    fm_trace_event(comp, type == FT_STACK_OVERFLOW ? EVT_STACK_FAULT : EVT_MEM_FAULT,
                   (uint16_t)(g_fm_noinit.cfsr & 0xFFFFU));
    rb_copy_last(&g_fm_rb, g_fm_noinit.history, FM_HISTORY_LEN);
    g_fm_noinit.crash_valid = 1U;
    __DSB();
    NVIC_SystemReset();
    for (;;) { }
}

void fm_trap(fm_trap_t kind, uint32_t exc_return)
{
    __disable_irq();

    uint32_t cfsr = SCB->CFSR;
    uint32_t hfsr = SCB->HFSR;
    uint32_t addr = 0;
    if (cfsr & SCB_CFSR_MMARVALID_Msk) {
        addr = SCB->MMFAR;
    } else if (cfsr & SCB_CFSR_BFARVALID_Msk) {
        addr = SCB->BFAR;
    }

    /* Exception from a task (thread mode on PSP): the stacked frame is on
     * the task's stack: r0 r1 r2 r3 r12 lr pc xpsr. If stacking itself
     * failed (MSTKERR / BusFault STKERR) the frame is garbage, so don't
     * trust it. A fault inside an interrupt (MSP) is recorded with pc 0. */
    uint32_t pc = 0, lr = 0;
    bool from_task = (exc_return & 0x4U) != 0U;
    uint32_t stk_err = cfsr & (SCB_CFSR_MSTKERR_Msk | SCB_CFSR_STKERR_Msk);
    if (from_task && !stk_err) {
        const uint32_t *frame = (const uint32_t *)__get_PSP();
        lr = frame[5];
        pc = frame[6];
    }

    uint8_t type = FT_MEM_ACCESS;
    if (stk_err || addr_in_guard(addr)) {
        type = FT_STACK_OVERFLOW;
    }

    g_fm_noinit.trap       = (uint8_t)kind;
    g_fm_noinit.pc         = pc;
    g_fm_noinit.lr         = lr;
    g_fm_noinit.cfsr       = cfsr;
    g_fm_noinit.hfsr       = hfsr;
    g_fm_noinit.fault_addr = addr;
    finish_crash_record(type, fm_current_comp());
}

void fm_trap_stack_hook(void *task_handle)
{
    __disable_irq();
    UBaseType_t tag = uxTaskGetTaskNumber((TaskHandle_t)task_handle);
    uint8_t comp = (tag == 0U || tag > FM_MAX_COMP) ? FM_COMP_OTHER : (uint8_t)(tag - 1U);
    g_fm_noinit.trap       = FM_TRAP_STACK_HOOK;
    g_fm_noinit.pc         = 0;
    g_fm_noinit.lr         = 0;
    g_fm_noinit.cfsr       = 0;
    g_fm_noinit.hfsr       = 0;
    g_fm_noinit.fault_addr = 0;
    finish_crash_record(FT_STACK_OVERFLOW, comp);
}

#else  /* FM_ENABLE_MONITOR == 0 */

void fm_capture_boot(void) {}
uint16_t fm_reset_cause(void) { return 0; }
const char *fm_reset_cause_str(uint16_t f) { (void)f; return "n/a"; }
void fm_trap(fm_trap_t kind, uint32_t exc_return)
{
    (void)kind; (void)exc_return;
    for (;;) { }      /* monitor compiled out: behave like the default handler */
}
void fm_trap_stack_hook(void *h) { (void)h; for (;;) { } }
void fm_mpu_init(void) {}
bool fm_mpu_protect_ro(const void *a, uint32_t s) { (void)a; (void)s; return false; }
void fm_mpu_guard_task(uint8_t c, void *h) { (void)c; (void)h; }
void fm_mpu_unguard_task(uint8_t c) { (void)c; }
uint32_t fm_stack_free_words(uint8_t c) { (void)c; return 0xFFFFU; }
uint32_t fm_task_pc(void *h) { (void)h; return 0; }

#endif /* FM_ENABLE_MONITOR */
