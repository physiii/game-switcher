#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#define BILL_ACCEPTOR_INPUT     4
#define GPIO_INPUT_PIN_SEL  ((1ULL<<BILL_ACCEPTOR_INPUT))
#define ESP_INTR_FLAG_DEFAULT 0

#define SPEAKER_POS    19
#define SPEAKER_NEG    18
#define VIDEO1_BTN     17
#define VIDEO2_BTN     12
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<SPEAKER_POS) | (1ULL<<SPEAKER_NEG) | (1ULL<<VIDEO1_BTN) | (1ULL<<VIDEO2_BTN))


int cnt = 0;
int bit_delay = 0;
bool tx_end = false;
bool already_printed_value = false;
int dollar_amount = 0;
bool tx_start = false;
bool tx_delay = false;

static xQueueHandle gpio_evt_queue = NULL;



void switch_input (int channel) {
    switch (channel) {
        case 1:
            printf("switching to channel %d\n", channel);
            gpio_set_level(SPEAKER_POS, false);
            gpio_set_level(SPEAKER_NEG, false);

            gpio_set_level(VIDEO1_BTN, true);
            vTaskDelay(500 / portTICK_RATE_MS);
            gpio_set_level(VIDEO1_BTN, false);
            break;

        case 2:
            printf("switching to channel %d\n", channel);
            gpio_set_level(SPEAKER_POS, true);
            gpio_set_level(SPEAKER_NEG, true);
            gpio_set_level(VIDEO2_BTN, true);
            vTaskDelay(500 / portTICK_RATE_MS);
            gpio_set_level(VIDEO2_BTN, false);
            break;

        printf("bad channel value %d\n", channel);

    }
}

void toggle_inputs (){
  switch_input(1);
  vTaskDelay(15000 / portTICK_RATE_MS);
  switch_input(2);
  vTaskDelay(15000 / portTICK_RATE_MS);
}


static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    dollar_amount++;
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
    if (!tx_start) {
      tx_start = true;
      tx_delay = true;
    }
}

static void gpio_task_example(void* arg)
{
    uint32_t io_num;
    switch_input(2); //start with channel 1
    for(;;) {

      if (tx_start) {
        tx_start = false;
        vTaskDelay(1000 / portTICK_RATE_MS);
        if (dollar_amount) {
  				dollar_amount = dollar_amount / 2;
          printf("{\"event_type\":\"bill_acceptor/\credit\", \"payload\":{\"value\":%d}}", dollar_amount);
          printf("\n");
        }
        dollar_amount = 0;
      } else {
  			vTaskDelay(1000 / portTICK_RATE_MS);
  		}

    }
}


void switch_main()
{

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    io_conf.intr_type = GPIO_PIN_INTR_POSEDGE;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    //change gpio intrrupt type for one pin
    gpio_set_intr_type(BILL_ACCEPTOR_INPUT, GPIO_INTR_ANYEDGE);

    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //start gpio task
    xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(BILL_ACCEPTOR_INPUT, gpio_isr_handler, (void*) BILL_ACCEPTOR_INPUT);

}
