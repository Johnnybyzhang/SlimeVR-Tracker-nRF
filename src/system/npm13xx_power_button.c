/*
 * nPM13xx SHPHLD power button.
 *
 * Hold the PMIC button for CONFIG_NPM13XX_POWER_BUTTON_HOLD_MS and release to
 * power off through the normal user-exit path (LED feedback, OTA deferral).
 * On battery with the button released the PMIC enters ship mode; with USB
 * present ship mode is impossible, so the MCU enters SYSTEMOFF with autonomous
 * charging left running and wakes on the next button press. A shutdown during
 * an unconfirmed MCUboot trial waits for confirmation: waking from SYSTEMOFF
 * is a reset, which would silently revert the update.
 *
 * Event handling follows Nordic's npm13xx_one_button sample.
 */
#include "globals.h"

/* Sources are globbed for every board; build only when enabled. */
#if CONFIG_NPM13XX_POWER_BUTTON
#include "system/system.h"
#include "system/npm13xx_power_button.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mfd/npm13xx.h>
#include <hal/nrf_gpio.h>
#if CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/mcuboot.h>
#endif

LOG_MODULE_REGISTER(power_button, LOG_LEVEL_INF);

#define PMIC_NODE DT_PARENT(DT_NODELABEL(pmic_charger))
BUILD_ASSERT(DT_NODE_HAS_PROP(PMIC_NODE, host_int_gpios),
	     "The power button needs the PMIC interrupt line (host-int-gpios)");

/* nPM13xx registers */
#define GPIO_BASE 0x06
#define GPIO_OFFSET_PDEN 0x0f
#define GPIO_OFFSET_OPENDRAIN 0x14
#define SHIP_BASE 0x0b
#define SHIP_OFFSET_TASK_SHIP_MODE 0x02
#define SHIP_OFFSET_STATUS 0x05 /* bit 0: SHPHLD pin level, low = pressed */
#define VBUSIN_BASE 0x02
#define VBUSIN_OFFSET_STATUS 0x07 /* bit 0: VBUS present */

static const struct device *const pmic = DEVICE_DT_GET(PMIC_NODE);
static const struct gpio_dt_spec irq = GPIO_DT_SPEC_GET(PMIC_NODE, host_int_gpios);
#define IRQ_PSEL NRF_DT_GPIOS_TO_PSEL(PMIC_NODE, host_int_gpios)
#define PMIC_INT_PIN DT_PROP(PMIC_NODE, pmic_int_pin)

static struct gpio_callback event_cb;
static K_SEM_DEFINE(request_sem, 0, 1);
static atomic_t pressed;
static atomic_t off_requested;
static int64_t press_ms;

static void event_handler(const struct device *dev, struct gpio_callback *cb, uint32_t events)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	if (events & BIT(NPM13XX_EVENT_SHIPHOLD_PRESS)) {
		press_ms = k_uptime_get();
		atomic_set(&pressed, 1);
	}
	if ((events & BIT(NPM13XX_EVENT_SHIPHOLD_RELEASE)) && atomic_cas(&pressed, 1, 0)) {
		int64_t held = k_uptime_get() - press_ms;
		if (held >= CONFIG_NPM13XX_POWER_BUTTON_HOLD_MS) {
			LOG_INF("Power button held %lld ms: shutdown requested", held);
			k_sem_give(&request_sem);
		}
	}
}

static int power_button_init(void)
{
	if (!device_is_ready(pmic)) {
		LOG_ERR("PMIC not ready");
		return -ENODEV;
	}
	/* The MFD driver selects GPIOx IRQ mode. Make it push-pull without the
	 * default pull-down, matching the active-high host input. */
	int err = mfd_npm13xx_reg_write(pmic, GPIO_BASE, GPIO_OFFSET_PDEN + PMIC_INT_PIN, 0);
	if (!err) {
		err = mfd_npm13xx_reg_write(pmic, GPIO_BASE, GPIO_OFFSET_OPENDRAIN + PMIC_INT_PIN, 0);
	}
	if (!err) {
		gpio_init_callback(&event_cb, event_handler,
				   BIT(NPM13XX_EVENT_SHIPHOLD_PRESS) | BIT(NPM13XX_EVENT_SHIPHOLD_RELEASE));
		err = mfd_npm13xx_add_callback(pmic, &event_cb);
	}
	uint8_t status;
	if (!err) {
		err = mfd_npm13xx_reg_read(pmic, SHIP_BASE, SHIP_OFFSET_STATUS, &status);
	}
	if (err) {
		LOG_ERR("Power button init failed: %d", err);
		return err;
	}
	if (!(status & BIT(0))) {
		/* Still held from the press that woke us: time it from boot. */
		press_ms = k_uptime_get();
		atomic_set(&pressed, 1);
	}
	return 0;
}
SYS_INIT(power_button_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static void power_button_thread(void)
{
	while (true) {
		k_sem_take(&request_sem, K_FOREVER);
#if CONFIG_BOOTLOADER_MCUBOOT
		if (!boot_is_img_confirmed() && mcuboot_swap_type() == BOOT_SWAP_TYPE_REVERT) {
			LOG_WRN("Shutdown deferred until the trial image is confirmed");
			while (!boot_is_img_confirmed()) {
				k_msleep(1000);
			}
		}
#endif
		atomic_set(&off_requested, 1);
		int err = sys_user_exit_request(false);
		if (err) {
			atomic_clear(&off_requested);
			LOG_WRN("Power button shutdown rejected: %d", err);
		}
	}
}
K_THREAD_DEFINE(power_button_thread_id, 1024, power_button_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

void npm13xx_power_button_arm_wake(void)
{
	nrf_gpio_cfg_sense_input(IRQ_PSEL, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_HIGH);
}

void npm13xx_power_button_prepare_poweroff(void)
{
	/* Let the MFD worker clear the pending release event before the level
	 * sense is armed, or it would wake the MCU immediately. */
	int64_t deadline = k_uptime_get() + 1000;
	while (gpio_pin_get_dt(&irq) > 0 && k_uptime_get() < deadline) {
		k_msleep(2);
	}
	npm13xx_power_button_arm_wake();
	if (!atomic_get(&off_requested)) {
		return; /* Other shutdowns keep their usual wake sources. */
	}
#if DT_NODE_HAS_PROP(DT_ALIAS(sw0), gpios)
	/* An intentional power-off wakes only on the power button. */
	nrf_gpio_cfg_sense_set(NRF_DT_GPIOS_TO_PSEL(DT_ALIAS(sw0), gpios), NRF_GPIO_PIN_NOSENSE);
#endif
	uint8_t shphld = 0, vbus = 1;
	int err = mfd_npm13xx_reg_read(pmic, SHIP_BASE, SHIP_OFFSET_STATUS, &shphld);
	if (!err) {
		err = mfd_npm13xx_reg_read(pmic, VBUSIN_BASE, VBUSIN_OFFSET_STATUS, &vbus);
	}
	if (!err && (shphld & BIT(0)) && !(vbus & BIT(0))) {
		/* Same task write as regulator_parent_ship_mode(), without needing
		 * regulator nodes. Power is removed shortly after. */
		LOG_INF("Entering PMIC ship mode");
		err = mfd_npm13xx_reg_write(pmic, SHIP_BASE, SHIP_OFFSET_TASK_SHIP_MODE, 1);
		k_msleep(20);
	}
	LOG_INF("Power off: SHPHLD=%u VBUS=%u err=%d; MCU SYSTEMOFF fallback", shphld & 1, vbus & 1, err);
}
#endif /* CONFIG_NPM13XX_POWER_BUTTON */
