#include <stdarg.h>
#include <stdio.h>

#include "usb_print.h"


#include "nrf.h"
#include "nrf_drv_usbd.h"
#include "nrf_drv_clock.h"
#include "nrf_drv_power.h"

#include "app_error.h"
#include "app_util.h"
#include "app_usbd_core.h"
#include "app_usbd.h"
#include "app_usbd_string_desc.h"
#include "app_usbd_cdc_acm.h"
#include "app_usbd_serial_num.h"

#include "boards.h"
#include "bsp.h"

#include "nrf_delay.h"

bool g_usd_message_active = false;

#define LED_USB_RESUME      (BSP_BOARD_LED_0)
#define LED_RED             (BSP_BOARD_LED_1)
#define LED_CDC_ACM_RX      (BSP_BOARD_LED_2)
#define LED_CDC_ACM_TX      (BSP_BOARD_LED_3)

#define NRF_LOG_INFO(X,...)

/**
 * @brief Enable power USB detection
 *
 * Configure if example supports USB port connection
 */
#ifndef USBD_POWER_DETECTION
#define USBD_POWER_DETECTION true
#endif


static void cdc_acm_user_ev_handler(app_usbd_class_inst_t const * p_inst,
                                    app_usbd_cdc_acm_user_event_t event);

#define CDC_ACM_COMM_INTERFACE  0
#define CDC_ACM_COMM_EPIN       NRF_DRV_USBD_EPIN2

#define CDC_ACM_DATA_INTERFACE  1
#define CDC_ACM_DATA_EPIN       NRF_DRV_USBD_EPIN1
#define CDC_ACM_DATA_EPOUT      NRF_DRV_USBD_EPOUT1


/**
 * @brief CDC_ACM class instance
 * */
APP_USBD_CDC_ACM_GLOBAL_DEF(m_app_cdc_acm,
                            cdc_acm_user_ev_handler,
                            CDC_ACM_COMM_INTERFACE,
                            CDC_ACM_DATA_INTERFACE,
                            CDC_ACM_COMM_EPIN,
                            CDC_ACM_DATA_EPIN,
                            CDC_ACM_DATA_EPOUT,
                            APP_USBD_CDC_COMM_PROTOCOL_AT_V250
);

#define READ_SIZE 1

static char m_rx_buffer[READ_SIZE];
static usb_rx_handler_t m_usb_rx_handler;
static bool g_is_tx_done = true;

/**
 * @brief User event handler @ref app_usbd_cdc_acm_user_ev_handler_t (headphones)
 * */
static void cdc_acm_user_ev_handler(app_usbd_class_inst_t const * p_inst,
                                    app_usbd_cdc_acm_user_event_t event)
{
    app_usbd_cdc_acm_t const * p_cdc_acm = app_usbd_cdc_acm_class_get(p_inst);

    switch (event)
    {
        case APP_USBD_CDC_ACM_USER_EVT_PORT_OPEN:
        {
            //bsp_board_led_on(LED_CDC_ACM_OPEN);

            /*Setup first transfer*/
            ret_code_t ret = app_usbd_cdc_acm_read(&m_app_cdc_acm,
                                                   m_rx_buffer,
                                                   READ_SIZE);
            UNUSED_VARIABLE(ret);
            break;
        }
        case APP_USBD_CDC_ACM_USER_EVT_PORT_CLOSE:
            //bsp_board_led_off(LED_CDC_ACM_OPEN);
            break;
        case APP_USBD_CDC_ACM_USER_EVT_TX_DONE:
            g_is_tx_done = true;
            //bsp_board_led_invert(LED_CDC_ACM_TX);
            break;
        case APP_USBD_CDC_ACM_USER_EVT_RX_DONE:
        {
            ret_code_t ret;
            NRF_LOG_INFO("Bytes waiting: %d", app_usbd_cdc_acm_bytes_stored(p_cdc_acm));
            do
            {
                /*Get amount of data transfered*/
                size_t size = app_usbd_cdc_acm_rx_size(p_cdc_acm);
                UNUSED_VARIABLE(size);
                NRF_LOG_INFO("RX: size: %lu char: %c", size, m_rx_buffer[0]);

                /* Fetch data until internal buffer is empty */
                ret = app_usbd_cdc_acm_read(&m_app_cdc_acm,
                                            m_rx_buffer,
                                            READ_SIZE);
                
                m_usb_rx_handler(m_rx_buffer[0]);

            } while (ret == NRF_SUCCESS);

            //bsp_board_led_invert(LED_CDC_ACM_RX);
            break;
        }
        default:
            break;
    }
}

static void usbd_user_ev_handler(app_usbd_event_type_t event)
{
    switch (event)
    {
        case APP_USBD_EVT_DRV_SUSPEND:
            ////bsp_board_led_off(LED_USB_RESUME);
            break;
        case APP_USBD_EVT_DRV_RESUME:
            ////bsp_board_led_on(LED_USB_RESUME);
            break;
        case APP_USBD_EVT_STARTED:

            break;
        case APP_USBD_EVT_STOPPED:
            app_usbd_disable();
            ////bsp_board_leds_off();
            break;
        case APP_USBD_EVT_POWER_DETECTED:
            NRF_LOG_INFO("USB power detected");

            if (!nrf_drv_usbd_is_enabled())
            {
                app_usbd_enable();
            }
            break;
        case APP_USBD_EVT_POWER_REMOVED:
            NRF_LOG_INFO("USB power removed");
            app_usbd_stop();
            break;
        case APP_USBD_EVT_POWER_READY:
            NRF_LOG_INFO("USB ready");
            app_usbd_start();
            break;
        default:
            break;
    }
}

void usb_print_init(usb_rx_handler_t handler)
{
    ret_code_t ret;

    m_usb_rx_handler = handler;

    static const app_usbd_config_t usbd_config = {
        .ev_state_proc = usbd_user_ev_handler
    };

    app_usbd_serial_num_generate();

    ret = app_usbd_init(&usbd_config);
    APP_ERROR_CHECK(ret);
    NRF_LOG_INFO("USBD CDC ACM example started.");

    app_usbd_class_inst_t const * class_cdc_acm = app_usbd_cdc_acm_class_inst_get(&m_app_cdc_acm);
    ret = app_usbd_class_append(class_cdc_acm);
    APP_ERROR_CHECK(ret);

    if (USBD_POWER_DETECTION)
    {
        ret = app_usbd_power_events_enable();
        APP_ERROR_CHECK(ret);
    }
    else
    {
        NRF_LOG_INFO("No USB power detection enabled\r\nStarting USB now");

        app_usbd_enable();
        app_usbd_start();
    }
}

void usb_print_loop()
{
    while (app_usbd_event_queue_process())
    {
        /* Nothing to do */
    }
}

bool usb_print_enabled()
{
    return g_usd_message_active;
}

void usb_print_enable(bool enable)
{
    g_usd_message_active = enable;
}

void usb_print(const void * p_buf,size_t length)
{
    ret_code_t ret;
    ret = app_usbd_cdc_acm_write(&m_app_cdc_acm, p_buf, length);
    APP_ERROR_CHECK(ret);
}

void usb_printf(const char *format, ...)
{
    LEDS_ON(LED_RED);
    if(g_usd_message_active)
    {
        char buffer[256];
        va_list args;
        va_start (args, format);
        size_t length = vsnprintf (buffer,256,format, args);
        if(length > 64)
        {
            length = 64;
            buffer[63] = '>';
        }
        ret_code_t ret;
        int count = 0;
        while(!g_is_tx_done)
        {
            app_usbd_event_queue_process();
            nrf_delay_ms(1);
            if(count++ > 100)
            {
                g_is_tx_done = true;
                return;
            }
        }
        g_is_tx_done = false;
        ret = app_usbd_cdc_acm_write(&m_app_cdc_acm, buffer, length);
        APP_ERROR_CHECK(ret);

        va_end (args);
    }
    
    LEDS_OFF(LED_RED);
}