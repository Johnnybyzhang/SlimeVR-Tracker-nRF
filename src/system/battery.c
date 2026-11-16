/*
 * Copyright (c) 2018-2019 Peter Bigot Consulting, LLC
 * Copyright (c) 2019-2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/dt-bindings/adc/nrf-saadc.h>
#ifdef CONFIG_ADC_NRFX_SAADC
#include <hal/nrf_saadc.h>
#endif
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <zephyr/logging/log.h>

#include "battery.h"

LOG_MODULE_REGISTER(BATTERY, CONFIG_ADC_LOG_LEVEL);

#define VBATT DT_PATH(battery_divider)
#define ZEPHYR_USER DT_PATH(zephyr_user)
#define PMIC_CHARGER DT_NODELABEL(pmic_charger)
#define USE_PMIC_CHARGER DT_NODE_HAS_STATUS(PMIC_CHARGER, okay)

#if USE_PMIC_CHARGER
static const struct device *const charger = DEVICE_DT_GET(PMIC_CHARGER);
#else

struct io_channel_config {
	uint8_t channel;
};

struct divider_config {
	struct io_channel_config io_channel;
	struct gpio_dt_spec power_gpios;
	/* output_ohm is used as a flag value: if it is nonzero then
	 * the battery is measured through a voltage divider;
	 * otherwise it is assumed to be directly connected to Vdd.
	 */
	uint32_t output_ohm;
	uint32_t full_ohm;
};

static const struct divider_config divider_config = {
#if DT_NODE_HAS_STATUS(VBATT, okay)
	.io_channel = {
		DT_IO_CHANNELS_INPUT(VBATT),
	},
	.power_gpios = GPIO_DT_SPEC_GET_OR(VBATT, power_gpios, {}),
	.output_ohm = DT_PROP(VBATT, output_ohms),
	.full_ohm = DT_PROP(VBATT, full_ohms),
#else /* /vbatt exists */
#error "Battery divider node does not exist"
	.io_channel = {
		DT_IO_CHANNELS_INPUT(ZEPHYR_USER),
	},
#endif /* /vbatt exists */
};

struct divider_data {
	const struct device* adc;
	struct adc_channel_cfg adc_cfg;
	struct adc_sequence adc_seq;
	int16_t raw;
};
static struct divider_data divider_data = {
#if DT_NODE_HAS_STATUS(VBATT, okay)
	.adc = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(VBATT)),
#else
#error "Battery divider node does not exist"
	.adc = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(ZEPHYR_USER)),
#endif
};
#endif

#if !USE_PMIC_CHARGER
#ifdef CONFIG_ADC_NRFX_SAADC
struct battery_adc_gain {
	enum adc_gain gain;
	uint8_t numerator;
	uint8_t denominator;
};

static int battery_select_gain(float max_adc_voltage, uint16_t reference_mv,
			       enum adc_gain *gain)
{
	static const struct battery_adc_gain gains[] = {
#if NRF_SAADC_HAS_GAIN_1_6
		{ ADC_GAIN_1_6, 1, 6 },
#endif
#if NRF_SAADC_HAS_GAIN_1_5
		{ ADC_GAIN_1_5, 1, 5 },
#endif
#if NRF_SAADC_HAS_GAIN_1_4
		{ ADC_GAIN_1_4, 1, 4 },
#endif
#if NRF_SAADC_HAS_GAIN_1_3
		{ ADC_GAIN_1_3, 1, 3 },
#endif
#if NRF_SAADC_HAS_GAIN_1_2
		{ ADC_GAIN_1_2, 1, 2 },
#endif
		{ ADC_GAIN_1, 1, 1 },
	};

	for (size_t i = ARRAY_SIZE(gains); i > 0; --i) {
		const struct battery_adc_gain *candidate = &gains[i - 1];

		/* Use the highest supported gain that covers the input range. */
		if (max_adc_voltage * 1000.0f * candidate->numerator <=
		    (float)reference_mv * candidate->denominator) {
			*gain = candidate->gain;
			return 0;
		}
	}

	return -ERANGE;
}
#endif

static int divider_setup(void) {
	const struct divider_config* cfg = &divider_config;
	const struct io_channel_config* iocp = &cfg->io_channel;
	const struct gpio_dt_spec* gcp = &cfg->power_gpios;
	struct divider_data* ddp = &divider_data;
	struct adc_sequence* asp = &ddp->adc_seq;
	struct adc_channel_cfg* accp = &ddp->adc_cfg;
	int rc;

	if (!device_is_ready(ddp->adc)) {
		LOG_ERR("ADC device is not ready %s", ddp->adc->name);
		return -ENOENT;
	}

	if (gcp->port) {
		if (!device_is_ready(gcp->port)) {
			LOG_ERR("%s: device not ready", gcp->port->name);
			return -ENOENT;
		}
		rc = gpio_pin_configure_dt(gcp, GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			LOG_ERR("Failed to control feed %s.%u: %d", gcp->port->name, gcp->pin, rc);
			return rc;
		}
	}

	*asp = (struct adc_sequence){
		.channels = BIT(0),
		.buffer = &ddp->raw,
		.buffer_size = sizeof(ddp->raw),
		.oversampling = 7, // TODO: using R3 board, ADC is very noisy, are other boards okay?
		.calibrate = true,
	};

#ifdef CONFIG_ADC_NRFX_SAADC
	enum adc_gain battery_adc_gain;

	float max_adc_voltage = cfg->output_ohm != 0 ? 5.0f * cfg->output_ohm / cfg->full_ohm : 3.6f; // Maximum voltage on input
	uint16_t reference_mv = adc_ref_internal(ddp->adc);

	rc = battery_select_gain(max_adc_voltage, reference_mv, &battery_adc_gain);
	if (rc != 0) {
		LOG_ERR("No ADC gain fits max voltage %.2f mV at %u mV reference: %d",
			(double)(max_adc_voltage * 1000.0f), reference_mv, rc);
		return rc;
	}

	LOG_INF("ADC gain enum: %d, max voltage: %.2f mV, reference: %u mV",
		battery_adc_gain, (double)(max_adc_voltage * 1000.0f), reference_mv);

	*accp = (struct adc_channel_cfg){
		.channel_id = 0,
		.gain = battery_adc_gain,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 3),
	};

	if (cfg->output_ohm != 0) {
		if (iocp->channel == 12) {
			accp->input_positive = NRF_SAADC_VDDHDIV5;
		} else {
			accp->input_positive = iocp->channel;
		}
	} else {
		accp->input_positive = NRF_SAADC_VDD;
	}

	if (iocp->channel == 12) { // VDDHDIV5
		asp->oversampling = 2;
		accp->acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10);
	}

	asp->resolution = 14;
#else /* CONFIG_ADC_var */
#error Unsupported ADC
#endif /* CONFIG_ADC_var */

	rc = adc_channel_setup(ddp->adc, accp);
	LOG_INF("Setup ADC input %u (io-channel %u) got %d", accp->input_positive, iocp->channel, rc);

	return rc;
}
#endif

static bool battery_ok;
#if USE_PMIC_CHARGER
static int64_t charger_sample_ms;
static bool charger_sample_valid;
#endif

static int battery_setup() {
#if USE_PMIC_CHARGER
	battery_ok = device_is_ready(charger);
	if (!battery_ok) {
		LOG_ERR("nPM1300 charger is not ready");
		return -ENODEV;
	}
	LOG_INF("Battery setup: nPM1300 charger ready");
	return 0;
#else
	int rc = divider_setup();

	battery_ok = (rc == 0);
	LOG_INF("Battery setup: %d %d", rc, battery_ok);
	return rc;
#endif
}

SYS_INIT(battery_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

int battery_measure_enable(bool enable) {
#if USE_PMIC_CHARGER
	ARG_UNUSED(enable);
	return battery_ok ? 0 : -ENODEV;
#else
	int rc = -ENOENT;

	if (battery_ok) {
		const struct gpio_dt_spec* gcp = &divider_config.power_gpios;

		rc = 0;
		if (gcp->port) {
			rc = gpio_pin_set_dt(gcp, enable);
		}
	}
	return rc;
#endif
}

int battery_sample(void) {
#if USE_PMIC_CHARGER
	charger_sample_valid = false;
	if (!battery_ok) {
		return -ENODEV;
	}

	int rc = sensor_sample_fetch(charger);
	if (rc != 0) {
		return rc;
	}

	struct sensor_value voltage;
	rc = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &voltage);
	if (rc == 0) {
		charger_sample_ms = k_uptime_get();
		charger_sample_valid = true;
	}
	return rc == 0 ? sensor_value_to_milli(&voltage) : rc;
#else
	int rc = -ENOENT;

	if (battery_ok) {
		struct divider_data* ddp = &divider_data;
		const struct divider_config* dcp = &divider_config;
		struct adc_sequence* sp = &ddp->adc_seq;

		rc = adc_read(ddp->adc, sp);
		sp->calibrate = false;
		if (rc == 0) {
			int32_t adc_uv = ddp->raw;

			rc = adc_raw_to_microvolts(
				adc_ref_internal(ddp->adc),
				ddp->adc_cfg.gain,
				sp->resolution,
				&adc_uv
			);
			if (rc != 0) {
				return rc;
			}

			/* Preserve sub-mV precision until after scaling the divider. */
			int64_t battery_uv = adc_uv;
			if (dcp->output_ohm != 0) {
				battery_uv = battery_uv * dcp->full_ohm / dcp->output_ohm;
			}
			rc = battery_uv / 1000;
			LOG_INF("raw %d ~ %d uV => %d mV\n", ddp->raw, adc_uv, rc);
		}
	}

	return rc;
#endif
}

#if USE_PMIC_CHARGER && CONFIG_BATTERY_FUEL_GAUGE
#include <nrf_fuel_gauge.h>

/* nPM13xx BCHGCHARGESTATUS bits */
#define CHG_STATUS_COMPLETE BIT(1)
#define CHG_STATUS_TRICKLE BIT(2)
#define CHG_STATUS_CC BIT(3)
#define CHG_STATUS_CV BIT(4)

static const struct battery_model fuel_gauge_model = {
#include CONFIG_BATTERY_FUEL_GAUGE_MODEL
};
static bool fuel_gauge_ready;
static int64_t fuel_gauge_ref_ms;
static int32_t fuel_gauge_chg_status;

static float sensor_float(const struct sensor_value *value)
{
	return (float)value->val1 + (float)value->val2 / 1000000.0f;
}

static int fuel_gauge_inform_charge_state(int32_t status)
{
	union nrf_fuel_gauge_ext_state_info_data state = {
		.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_IDLE,
	};
	if (status & CHG_STATUS_COMPLETE) {
		state.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_COMPLETE;
	} else if (status & CHG_STATUS_TRICKLE) {
		state.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_TRICKLE;
	} else if (status & CHG_STATUS_CC) {
		state.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_CC;
	} else if (status & CHG_STATUS_CV) {
		state.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_CV;
	}
	return nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_STATE_CHANGE,
					       &state);
}

static int fuel_gauge_start(float voltage, float current, float temp)
{
	struct nrf_fuel_gauge_init_parameters parameters =
		NRF_FUEL_GAUGE_DEFAULT_INIT_PARAMETERS_SECONDARY(voltage, current, temp,
								 &fuel_gauge_model);
	int rc = nrf_fuel_gauge_init(&parameters, NULL);
	if (rc) {
		return rc;
	}

	/* Charge and termination currents come from the devicetree charger
	 * configuration; the gauge never changes them. */
	struct sensor_value value;
	rc = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, &value);
	if (rc) {
		return rc;
	}
	float limit = sensor_float(&value);
	rc = nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_CURRENT_LIMIT,
		&(union nrf_fuel_gauge_ext_state_info_data){.charge_current_limit = limit});
	if (rc) {
		return rc;
	}
	rc = nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_TERM_CURRENT,
		&(union nrf_fuel_gauge_ext_state_info_data){
			.charge_term_current = limit * DT_PROP(PMIC_CHARGER, term_current_percent) / 100.0f});
	if (rc) {
		return rc;
	}
	fuel_gauge_chg_status = -1;
	fuel_gauge_ref_ms = k_uptime_get();
	LOG_INF("nRF Fuel Gauge %s started: %s", nrf_fuel_gauge_version, fuel_gauge_model.name);
	return 0;
}

/* Uses the sample fetched by battery_sample(). Returns SOC in pptt or a
 * negative error; invalid measurements suppress the SOC instead of inventing
 * one. */
static int fuel_gauge_pptt(int voltage_mV)
{
	struct sensor_value value;
	int rc = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
	if (rc) {
		return rc;
	}
	/* Zephyr: negative current discharges. nRF Fuel Gauge: positive discharges. */
	float current = -sensor_float(&value);
	rc = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_TEMP, &value);
	if (rc) {
		return rc;
	}
	float temp = sensor_float(&value);
	rc = sensor_channel_get(charger, SENSOR_CHAN_NPM13XX_CHARGER_STATUS, &value);
	if (rc) {
		return rc;
	}
	int32_t status = value.val1;
	rc = sensor_attr_get(charger, SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS,
		SENSOR_ATTR_NPM13XX_CHARGER_VBUS_PRESENT, &value);
	if (rc) {
		return rc;
	}
	bool vbus = value.val1 != 0;

	float voltage = voltage_mV / 1000.0f;
	if (voltage < 2.0f || voltage > 4.5f || !isfinite(current) || !isfinite(temp)
		|| temp < -40.0f || temp > 85.0f) {
		if (voltage < 2.0f) {
			fuel_gauge_ready = false; /* Battery absent: restart from the next valid cell. */
		}
		return -ERANGE;
	}
	if (!fuel_gauge_ready) {
		rc = fuel_gauge_start(voltage, current, temp);
		if (rc) {
			LOG_ERR("nRF Fuel Gauge start failed: %d", rc);
			return rc;
		}
		fuel_gauge_ready = true;
	}

	rc = nrf_fuel_gauge_ext_state_update(vbus ? NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_CONNECTED
						  : NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_DISCONNECTED, NULL);
	if (!rc && status != fuel_gauge_chg_status) {
		rc = fuel_gauge_inform_charge_state(status);
		if (!rc) {
			fuel_gauge_chg_status = status;
		}
	}
	float soc;
	if (!rc) {
		float delta = MAX((float)k_uptime_delta(&fuel_gauge_ref_ms) / 1000.0f, 0.001f);
		rc = nrf_fuel_gauge_process(voltage, current, temp, delta, &soc, NULL);
	}
	if (!rc && !isfinite(soc)) {
		rc = -ERANGE;
	}
	if (rc) {
		return rc;
	}
	return (int)(CLAMP(soc, 0.0f, 100.0f) * 100.0f + 0.5f);
}
#endif

int battery_charger_state(bool *plugged, bool *charging, bool *charged)
{
#if USE_PMIC_CHARGER
	if (!battery_ok) {
		return -ENODEV;
	}

	struct sensor_value value;
	int rc = sensor_attr_get(charger, SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS,
		SENSOR_ATTR_NPM13XX_CHARGER_VBUS_PRESENT, &value);
	if (rc != 0) {
		return rc;
	}
	*plugged = value.val1 != 0;

	rc = sensor_channel_get(charger, SENSOR_CHAN_NPM13XX_CHARGER_STATUS, &value);
	if (rc != 0) {
		return rc;
	}
	*charged = (value.val1 & BIT(1)) != 0;
	*charging = (value.val1 & (BIT(2) | BIT(3) | BIT(4))) != 0;
	return 0;
#else
	ARG_UNUSED(plugged);
	ARG_UNUSED(charging);
	ARG_UNUSED(charged);
	return -ENOTSUP;
#endif
}

int battery_charger_snapshot(bool *plugged, bool *charging, bool *charged)
{
#if USE_PMIC_CHARGER
	/* The power owner fetches each 100 ms iteration. Preserve the 1500 ms
	 * freshness allowance, but reject stopped or incompletely read samples. */
	if (!charger_sample_valid || k_uptime_get() - charger_sample_ms > 1500) return -EAGAIN;
	bool external, active, full;
	int err = battery_charger_state(&external, &active, &full);
	if (err) return err;
	*plugged = external;
	*charging = active;
	*charged = full && !active;
	return 0;
#else
	ARG_UNUSED(plugged);
	ARG_UNUSED(charging);
	ARG_UNUSED(charged);
	return -ENOTSUP;
#endif
}

unsigned int
battery_level_pptt(unsigned int batt_mV, const struct battery_level_point* curve) {
	const struct battery_level_point* pb = curve;

	if (batt_mV >= pb->lvl_mV) {
		/* Measured voltage above highest point, cap at maximum. */
		return pb->lvl_pptt;
	}
	/* Go down to the last point at or below the measured voltage. */
	while ((pb->lvl_pptt > 0) && (batt_mV < pb->lvl_mV)) {
		++pb;
	}
	if (batt_mV < pb->lvl_mV) {
		/* Below lowest point, cap at minimum */
		return pb->lvl_pptt;
	}

	/* Linear interpolation between below and above points. */
	const struct battery_level_point* pa = pb - 1;

	return pb->lvl_pptt
		 + ((pa->lvl_pptt - pb->lvl_pptt) * (batt_mV - pb->lvl_mV)
			/ (pa->lvl_mV - pb->lvl_mV));
}

static const struct battery_level_point __maybe_unused levels[] = {
#if CONFIG_BATTERY_USE_REG_BUCK_MAPPING
	{10000, 4150},
	{9500, 4075},
	{3000, 3775},
	{500, 3450},
	{0, 3200},
#elif CONFIG_BATTERY_USE_REG_LDO_MAPPING
	{10000, 4150},
	{9500, 4025},
	{3000, 3650},
	{500, 3400},
	{0, 3200},
#else
#if !CONFIG_BATTERY_NO_MAPPING
#warning "Battery voltage map not defined"
#endif
	{10000, 0},
	{0, 0},
#endif
};

int read_batt_mV(int* out) {
	int rc = battery_measure_enable(true);

	if (rc != 0) {
		LOG_ERR("Failed initialize battery measurement: %d", rc);
		if (out != NULL) {
			*out = -1;
		}
		return rc;
	}

	/* Honor slow measurement switches while retaining the existing 200 us
	 * minimum for boards using the binding's shorter default.
	 */
	k_usleep(MAX(200, DT_PROP_OR(VBATT, power_on_sample_delay_us, 200)));

	int batt_mV = battery_sample();

	if (batt_mV < 0) {
		LOG_DBG("Failed to read battery voltage: %d", batt_mV);
	}

	battery_measure_enable(false);

	if (out != NULL) {
		*out = batt_mV;
	}

	if (batt_mV < 0) {
		return batt_mV;
	}

#if USE_PMIC_CHARGER && CONFIG_BATTERY_FUEL_GAUGE
	return fuel_gauge_pptt(batt_mV);
#else
	return (int)battery_level_pptt((unsigned int)batt_mV, levels);
#endif
}
