/*
 * ESP32 "Factory" WLAN Config + Factory Setup app
 *
 * Copyright (C) 2017 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */
#include <libwebsockets.h>
#include <nvs_flash.h>
#include "soc/ledc_reg.h"
#include "driver/ledc.h"

extern void
switch_to_channel_1(void);

extern void
switch_to_channel_2(void);

/* protocol for scan updates over ws and saving wlan setup */
#include "protocol_esp32_lws_scan.c"
/* protocol for OTA update using POST / https / browser upload */
#include "protocol_esp32_lws_ota.c"
/* protocol for ACME cert management */
#include "acme-client/protocol_lws_acme_client.c"

static struct lws_context *context;
static int id_flashes;

static const struct lws_protocols protocols_ap[] = {
	{
		"http-only",
		lws_callback_http_dummy,
		0,	/* per_session_data_size */
		0, 0, NULL, 900
	},
	LWS_PLUGIN_PROTOCOL_ESPLWS_SCAN,
	LWS_PLUGIN_PROTOCOL_ESPLWS_OTA,
	LWS_PLUGIN_PROTOCOL_LWS_ACME_CLIENT,

	{ NULL, NULL, 0, 0, 0, NULL, 0 } /* terminator */
};

/* ACME setup */

static const struct lws_protocol_vhost_options ap_pvo_acme_settings_6 = {
	NULL,
	NULL,
	"email",
	lws_esp32.le_email
};

static const struct lws_protocol_vhost_options ap_pvo_acme_settings_5 = {
	&ap_pvo_acme_settings_6,
	NULL,
	"common-name",
	lws_esp32.le_dns
};

static const struct lws_protocol_vhost_options ap_pvo_acme_settings_4 = {
	&ap_pvo_acme_settings_5,
	NULL,
	"key-path",
	"ap-key.pem"
};

static const struct lws_protocol_vhost_options ap_pvo_acme_settings_3 = {
	&ap_pvo_acme_settings_4,
	NULL,
	"cert-path",
	"ap-cert.pem"
};

static const struct lws_protocol_vhost_options ap_pvo_acme_settings_2 = {
	&ap_pvo_acme_settings_3,
	NULL,
	"auth-path",
	"le-auth-jwk"
};

static const struct lws_protocol_vhost_options ap_pvo_acme_settings_1 = {
	&ap_pvo_acme_settings_2,
	NULL,
	"directory-url",
	// "https://acme-v01.api.letsencrypt.org/directory" /* real */
	"https://acme-staging.api.letsencrypt.org/directory" /* staging */
};

static const struct lws_protocol_vhost_options ap_pvo_acme = {
	NULL,
	&ap_pvo_acme_settings_1,
	"lws-acme-client",
	""
};

static const struct lws_protocol_vhost_options pvo_headers = {
	NULL,
	NULL,
	"Keep-Alive:",
	"timeout=15, max=20",
};

static const struct lws_protocol_vhost_options ap_pvo2 = {
	&ap_pvo_acme,
	NULL,
	"esplws-ota",
	""
};

static const struct lws_protocol_vhost_options ap_pvo = {
	&ap_pvo2,
	NULL,
	"esplws-scan",
	""
};

static const struct lws_http_mount mount_ota_post = {
	.mountpoint		= "/otaform",
	.origin			= "esplws-ota",
	.origin_protocol	= LWSMPRO_CALLBACK,
	.mountpoint_len		= 8,
};

static const struct lws_http_mount mount_ap_post = {
	.mount_next		= &mount_ota_post,
	.mountpoint		= "/factory",
	.origin			= "esplws-scan",
	.origin_protocol	= LWSMPRO_CALLBACK,
	.mountpoint_len		= 8,
};

static struct lws_http_mount mount_ap = {
	.mount_next		= &mount_ap_post,
	.mountpoint		= "/",
        .origin			= "/ap",
        .def			= "index.html",
        .origin_protocol	= LWSMPRO_FILE,
        .mountpoint_len		= 1,
	.cache_max_age		= 300000,
	.cache_reusable		= 1,
        .cache_revalidate       = 1,
        .cache_intermediaries   = 1,
};

void lws_esp32_leds_timer_cb(TimerHandle_t th)
{
	struct timeval t;
	unsigned long r;
	int div = 3 - (2 * !!lws_esp32.inet);
	int base = 4096 * !lws_esp32.inet;

	gettimeofday(&t, NULL);
	r = ((t.tv_sec * 1000000) + t.tv_usec);

	if (!id_flashes)
		ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0,
				base + (lws_esp32_sine_interp(r / (1699 - (500 * !lws_esp32.inet))) / div));
	else
		ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, lws_esp32_sine_interp(r / 333));

	ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

	if (id_flashes) {
		id_flashes++;
		if (id_flashes == 500)
			id_flashes = 0;
	}
}

esp_err_t event_handler(void *ctx, system_event_t *event)
{
	return lws_esp32_event_passthru(ctx, event);
}

#define GPIO_ID 23


/*
 * This is called when the user asks to "Identify physical device"
 * he is configuring, by pressing the Identify button on the AP
 * setup page for the device.
 *
 * It should do something device-specific that
 * makes it easy to identify which physical device is being
 * addressed, like flash an LED on the device on a timer for a
 * few seconds.
 */
void
lws_esp32_identify_physical_device(void)
{
	lwsl_notice("HIT %s\n", __func__);

	id_flashes = 1;
}

void
switch_to_channel_1(void)
{
	lwsl_notice("HIT %s\n", __func__);
}


void
switch_to_channel_2(void)
{
	lwsl_notice("HIT %s\n", __func__);
}


void
lws_esp32_button(int down)
{
	lwsl_notice("button %d\n", down);
	if (!context)
		return;
	lws_callback_on_writable_all_protocol(context, &protocols_ap[1]);
}

void app_main(void)
{
	static struct lws_context_creation_info info;
	struct lws_vhost *vh;
        ledc_channel_config_t ledc_channel = {
            .channel = LEDC_CHANNEL_0,
            .duty = 8191,
            .gpio_num = GPIO_ID,
            .intr_type = LEDC_INTR_FADE_END,
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .timer_sel = LEDC_TIMER_0,
        };

	ledc_channel_config(&ledc_channel);

	lws_esp32_set_creation_defaults(&info);

	info.vhost_name = "ap";
	info.protocols = protocols_ap;
	info.mounts = &mount_ap;
	info.pvo = &ap_pvo;
	info.headers = &pvo_headers;
	info.ssl_cert_filepath = "ap-cert.pem";
	info.ssl_private_key_filepath = "ap-key.pem";
	info.simultaneous_ssl_restriction = 4;
	info.max_http_header_pool = 8;

	nvs_flash_init();
	lws_esp32_wlan_config();

	ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL));

	lws_esp32_wlan_start_ap();
	/* this configures the LED timer channel 0 and starts the fading cb */
	context = lws_esp32_init(&info, &vh);

	while (!lws_service(context, 10))
                taskYIELD();
}
