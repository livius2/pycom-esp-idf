// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdbool.h>
#include <stddef.h>
#include "esp_log.h"
#include "soc/sdmmc_struct.h"
#include "soc/sdmmc_reg.h"
#include "soc/io_mux_reg.h"
#include "sdmmc_periph.h"
#include "esp_intr_alloc.h"
#include "esp_intr.h"
#include "esp_attr.h"
#include "driver/gpio.h"

#define SDMMC_PIN_CLK   PERIPHS_IO_MUX_MTMS_U
#define SDMMC_PIN_CMD   PERIPHS_IO_MUX_MTDO_U
#define SDMMC_PIN_D0    PERIPHS_IO_MUX_GPIO2_U
#define SDMMC_PIN_D1    PERIPHS_IO_MUX_GPIO4_U
#define SDMMC_PIN_D2    PERIPHS_IO_MUX_MTDI_U
#define SDMMC_PIN_D3    PERIPHS_IO_MUX_MTCK_U
#define SDMMC_PIN_FUNC  3

#define SDMMC_MAX_DS_CLK_FREQ_KHZ 25000
#define SDMMC_MAX_HS_CLK_FREQ_KHZ 50000

#define MAX(a, b) (((a) > (b))?(a):(b))

static const char* TAG = "sdmmc_periph";

static IRAM_ATTR void sdmmc_isr(void* arg);
static void sdmmc_idma_init();

static intr_handle_t s_intr_handle;

void sdmmc_reset()
{
    // Set reset bits
    SDMMC.ctrl.controller_reset = 1;
    SDMMC.ctrl.dma_reset = 1;
    SDMMC.ctrl.fifo_reset = 1;
    // Wait for the reset bits to be cleared by hardware
    while (SDMMC.ctrl.controller_reset || SDMMC.ctrl.fifo_reset || SDMMC.ctrl.dma_reset) {
        ;
    }
}


esp_err_t sdmmc_clk_cfg(uint32_t max_freq_khz)
{
    /* We have two clock divider stages:
     * - one is the clock generator which drives SDMMC peripheral,
     *   it can be configured using SDMMC.clock register. It can generate
     *   frequencies 160MHz/(N + 1), where 0 < N < 16, I.e. from 10 to 80 MHz.
     * - 4 clock dividers inside SDMMC peripheral, which can divide clock
     *   from the first stage by 2 * M, where 0 < M < 255
     *   (they can also be bypassed).
     *
     *
     * For cards which aren't UHS-1 or UHS-2 cards, which we don't support,
     * maximum bus frequency in high speed (HS) mode is 50 MHz.
     * Note: for non-UHS-1 cards, HS mode is optional.
     * Default speed (DS) mode is mandatory, it works up to 25 MHz.
     * Whether the card supports HS or not can be determined using TRAN_SPEED
     * field of card's CSD register.
     *
     * 50 MHz can not be obtained exactly, closest we can get is 53 MHz.
     * For now set the first stage divider to generate 40MHz, and then configure
     * the second stage dividers to generate
     *  - 40MHz (to be used for HS)
     *  - 20MHz (to be used for DS)
     *  - 400kHz (to be used for probing)
     *
     * If max_freq_khz < 40000, we cap all frequencies to this maximum,
     * subject to rounding error.
     */
    SDMMC.clock.div_factor_l = 3;
    SDMMC.clock.div_factor_h = 1;
    SDMMC.clock.div_factor_p = 3;

    const int clk40m = 40000;

    int min_div = 0;
    if (max_freq_khz < clk40m) {
        // round up; extra *2 is because clock divider divides by 2*n
        min_div = (clk40m + max_freq_khz * 2 - 1) / (max_freq_khz * 2);
    }

    SDMMC.clkdiv.div0 = MAX(0, min_div);  // bypass, 40MHz
    SDMMC.clkdiv.div1 = MAX(1, min_div);  // divide by 2, 20MHz
    SDMMC.clkdiv.div2 = MAX(50, min_div); // divide by 100, 400kHz
    SDMMC.clksrc.card1 = 2;

    // Wait for the clock to propagate
    ets_delay_us(10);

    ESP_LOGD(TAG, "clock dividers: %d, %d, %d", SDMMC.clkdiv.div0, SDMMC.clkdiv.div1, SDMMC.clkdiv.div2);
    ESP_LOGD(TAG, "frequencies (kHz): %d, %d, %d",
            (SDMMC.clkdiv.div0 == 0) ? clk40m : clk40m / (2 * SDMMC.clkdiv.div0),
            clk40m / (2 * SDMMC.clkdiv.div1),
            clk40m / (2 * SDMMC.clkdiv.div2));

    return ESP_OK;
}

esp_err_t sdmmc_periph_set_speed(sdmmc_periph_speed_t speed)
{
    uint32_t src = 2;
    // This has to match the assignment of clkdiv registers above
    switch (speed) {
        case SDMMC_PERIPH_SPEED_HIGH:
            src = 0;    // clkdiv0
            break;
        case SDMMC_PERIPH_SPEED_DEFAULT:
            src = 1;    // clkdiv1
            break;
        case SDMMC_PERIPH_SPEED_LOW:
            src = 2;    // clkdiv2
            break;
    }
    // Clock update command (not a real command; just updates CIU registers)
    sdmmc_hw_cmd_t cmd_val = {
        .card_num = 1,
        .update_clk_reg = 1
    };
    // Disable clock first
    SDMMC.clkena.cclk_enable = 0;
    sdmmc_start_command(cmd_val, 0);

    // Set new values and enable
    SDMMC.clkena.cclk_enable = BIT(1);
    SDMMC.clkena.cclk_low_power = BIT(1);
    SDMMC.clksrc.card1 = src;
    sdmmc_start_command(cmd_val, 0);

    return ESP_OK;
}

void sdmmc_start_command(sdmmc_hw_cmd_t cmd, uint32_t arg) {
    while (SDMMC.cmd.start_command) {
        ;
    }
    SDMMC.cmdarg = arg;
    cmd.start_command = 1;
    SDMMC.cmd = cmd;
}

esp_err_t sdmmc_hw_init(uint32_t max_freq_khz, QueueHandle_t event_queue)
{
    esp_err_t ret;
    sdmmc_reset();

    ESP_LOGD(TAG, "peripheral version %x, hardware config %08x", SDMMC.verid, SDMMC.hcon);

    // Clear interrupt status and set interrupt mask to known state
    SDMMC.rintsts.val = 0xffffffff;
    SDMMC.intmask.val = 0;
    SDMMC.ctrl.int_enable = 0;

    // Attach the interrupt handler
    ESP_INTR_DISABLE(19);
    intr_matrix_set(xPortGetCoreID(), ETS_SDIO_HOST_INTR_SOURCE, 19);
    xt_set_interrupt_handler(19, &sdmmc_isr, event_queue);
    ESP_INTR_ENABLE(19);

    // Enable interrupts
    SDMMC.intmask.val =
            SDMMC_INTMASK_CD |
            SDMMC_INTMASK_CMD_DONE |
            SDMMC_INTMASK_DATA_OVER |
            SDMMC_INTMASK_RCRC | SDMMC_INTMASK_DCRC |
            SDMMC_INTMASK_RTO | SDMMC_INTMASK_DTO | SDMMC_INTMASK_HTO |
            SDMMC_INTMASK_SBE | SDMMC_INTMASK_EBE |
            SDMMC_INTMASK_RESP_ERR | SDMMC_INTMASK_HLE;
    SDMMC.ctrl.int_enable = 1;

    // Enable DMA
    sdmmc_idma_init();

    // Configure pins
    const uint32_t sdmmc_pins[] = {
            SDMMC_PIN_CLK,
            SDMMC_PIN_CMD,
            SDMMC_PIN_D0,
    };
    const size_t sdmmc_pin_count = sizeof(sdmmc_pins) / sizeof(sdmmc_pins[0]);

    for (size_t i = 0; i < sdmmc_pin_count; ++i) {
        PIN_INPUT_ENABLE(sdmmc_pins[i]);
        PIN_FUNC_SELECT(sdmmc_pins[i], SDMMC_PIN_FUNC);
        PIN_SET_DRV(sdmmc_pins[i], 3);
    }
    gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(14, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);

    // Configure clock
    ret = sdmmc_clk_cfg(max_freq_khz);
    if (ret != ESP_OK) {
        return ret;
    }

    // Select low speed by default
    sdmmc_periph_set_speed(SDMMC_PERIPH_SPEED_LOW);

    // Set 1-bit bus by default
    sdmmc_periph_set_bus_width(1);

    return ESP_OK;
}

esp_err_t sdmmc_periph_set_bus_width(int width)
{
    if (width == 1) {
        SDMMC.ctype.card_width &= ~BIT(1);
    } else if (width == 4) {
        SDMMC.ctype.card_width |= BIT(1);
    } else if (width == 8){
        return ESP_ERR_NOT_SUPPORTED;   // TODO: support 8-bit MMC mode
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void sdmmc_idma_init()
{
    SDMMC.ctrl.dma_enable = 1;
    SDMMC.bmod.val = 0;
    SDMMC.bmod.sw_reset = 1;
    SDMMC.idinten.ni = 1;
    SDMMC.idinten.ri = 1;
    SDMMC.idinten.ti = 1;
}


void sdmmc_idma_stop()
{
    SDMMC.ctrl.use_internal_dma = 0;
    SDMMC.ctrl.dma_reset = 1;
    SDMMC.bmod.fb = 0;
    SDMMC.bmod.enable = 0;
}

void sdmmc_idma_prepare_transfer(sdmmc_desc_t* desc, size_t block_size, size_t data_size)
{
    // TODO: set timeout depending on data size
    SDMMC.tmout.val = 0xffffffff;

    // Set size of data and DMA descriptor pointer
    SDMMC.bytcnt = data_size;
    SDMMC.blksiz = block_size;
    SDMMC.dbaddr = desc;

    // Enable everything needed to use DMA
    SDMMC.ctrl.dma_enable = 1;
    SDMMC.ctrl.use_internal_dma = 1;
    SDMMC.bmod.enable = 1;
    SDMMC.bmod.fb = 1;
    sdmmc_idma_resume();
}

void sdmmc_idma_resume()
{
    SDMMC.pldmnd = 1;
}

/* Ignoring SDIO and streaming read/writes for now (and considering just SD memory cards),
 * all communication is driven by the master, and the hardware handles things like stop
 * commands automatically. So the interrupt handler doesn't need to do much, we just push
 * interrupt status into a queue, clear interrupt flags, and let the task currently doing
 * communication figure out what to do next.
 *
 * Card detect interrupts pose a small issue though, because if a card is plugged in and
 * out a few times, while there is not task to process the events, event queue can become
 * full and some card detect events may be dropped. We ignore this problem for now, since
 * the there are no other interesting events which can get lost due to this.
 */

static IRAM_ATTR void sdmmc_isr(void* arg) {
    QueueHandle_t queue = (QueueHandle_t) arg;
    sdmmc_event_t event;
    uint32_t pending = SDMMC.mintsts.val;
    SDMMC.rintsts.val = pending;
    event.sdmmc_status = pending;

    uint32_t dma_pending = SDMMC.idsts.val;
    SDMMC.idsts.val = dma_pending;
    event.dma_status = dma_pending & 0x1f;

    int higher_priority_task_awoken = pdFALSE;
    xQueueSendFromISR(queue, &event, &higher_priority_task_awoken);
    if (higher_priority_task_awoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

