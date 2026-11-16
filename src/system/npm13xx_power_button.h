#ifndef SLIMENRF_SYSTEM_NPM13XX_POWER_BUTTON_H
#define SLIMENRF_SYSTEM_NPM13XX_POWER_BUTTON_H

#include <stdbool.h>

#if CONFIG_NPM13XX_POWER_BUTTON
/* Arm the PMIC interrupt line as a SYSTEMOFF wake source (all sleep paths). */
void npm13xx_power_button_arm_wake(void);
/* Final step of a system-off: for a power-button shutdown, disarm sw0 wake and
 * enter PMIC ship mode when on battery with the button released. */
void npm13xx_power_button_prepare_poweroff(void);
#else
static inline void npm13xx_power_button_arm_wake(void) {}
static inline void npm13xx_power_button_prepare_poweroff(void) {}
#endif

#endif
