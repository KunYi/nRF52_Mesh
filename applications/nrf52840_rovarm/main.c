/** @file main.c
 *
 * main entry for the application nRF52_dongle
 * 
 * @author Wassim FILALI
 *
 * @compiler arm gcc
 *
 *
 * $Date: 01.06.2018 adding doxy header this file existed since the repo creation
 *
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "sdk_common.h"
#include "nrf.h"
#include "nrf_error.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "boards.h"
#include "app_util.h"

#include "bsp.h"

//for the log
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

// --------------------- inputs from sdk_config --------------------- 
// ---> TWI0_ENABLED ---> TWI1_ENABLED
#include "uicr_user_defines.h"
//drivers
//apps
#include "clocks.h"
#include "mesh.h"
#include "app_ser.h"

#include "nrf_mtx.h"

char rtc_message[64];
char uart_message[64];
uint32_t uart_rx_size=0;

#define GPIO_CUSTOM_Debug 10

#define led2_green_on()   bsp_board_led_on(0)
#define led2_green_off()  bsp_board_led_off(0)
#define led1_red_on()     bsp_board_led_on(1)
#define led1_red_off()    bsp_board_led_off(1)
#define led1_green_on()    bsp_board_led_on(2)
#define led1_green_off()   bsp_board_led_off(2)
#define led1_blue_on()   bsp_board_led_on(3)
#define led1_blue_off()  bsp_board_led_off(3)

extern uint32_t ser_evt_tx_count;

void blink_green1(int time,int afteroff)
{
    led1_green_on();
    nrf_delay_ms(time);
    led1_green_off();
    if(afteroff)
    {
        nrf_delay_ms(afteroff);
    }
}

void blink_green(int time,int afteroff)
{
    led2_green_on();
    nrf_delay_ms(time);
    led2_green_off();
    if(afteroff)
    {
        nrf_delay_ms(afteroff);
    }
}

void blink_red(int time,int afteroff)
{
    led1_red_on();
    nrf_delay_ms(time);
    led1_red_off();
    if(afteroff)
    {
        nrf_delay_ms(afteroff);
    }
}

void blink_blue(int time,int afteroff)
{
    led1_blue_on();
    nrf_delay_ms(time);
    led1_blue_off();
    if(afteroff)
    {
        nrf_delay_ms(afteroff);
    }
}

void blink()
{
    nrf_delay_ms(500);
    blink_red(200,500);
    blink_blue(500,500);
    bsp_board_leds_on();
}

/**
 * @brief callback from the RF Mesh stack on valid packet received for this node
 * 
 * @param msg message structure with packet parameters and payload
 */
void rf_mesh_handler(message_t* msg)
{
    bool is_relevant_host = false;
    NRF_LOG_INFO("rf_mesh_handler()");
    if(MESH_IS_BROADCAST(msg->control))
    {
        is_relevant_host = true;
    }
    else if((msg->dest == get_this_node_id()))
    {
        if(MESH_IS_RESPONSE(msg->control))
        {
            is_relevant_host = true;
        }
        //else it's a request or a message
        else if( (msg->pid == Mesh_Pid_ExecuteCmd) && (UICR_is_rf_cmd()) )
        {
            mesh_execute_cmd(msg->payload,msg->payload_length,true,msg->source);
        }
    }
    if(is_relevant_host)
    {
        //Pure routers should not waste time sending messages over uart
        if(UICR_is_rf2uart())
        {
            char rf_message[128];
            mesh_parse(msg,rf_message);
            ser_send(rf_message);
        }
    }
}

/**
 * @brief called only with a full line message coming from UART 
 * ending with '\r', '\n' or '0'
 * @param msg contains a pointer to the DMA buffer, so do not keep it after the call
 * @param size safe managemnt with known size, does not include the ending char '\r' or other
 */
//#define UART_MIRROR
void app_serial_handler(const char*msg,uint8_t size)
{
    uart_rx_size+= size;
    //the input (msg) is really the RX DMA pointer location
    //and the output (uart_message) is reall the TX DMA pointer location
    //so have to copy here to avoid overwriting
    #ifdef UART_MIRROR
        memcpy(uart_message,msg,size);
        sprintf(uart_message+size,"\r\n");//Add line ending and NULL terminate it with sprintf
        ser_send(uart_message);
    #endif

    if(UICR_is_uart_cmd())
    {
        mesh_text_request(msg,size);
    }
}

/**
 * @brief Callback from after completion of the cmd request
 * Note this is a call back as some undefined latency and events might happen
 * before the response is ready, thus the requests cannot always return immidiatly
 * 
 * @param text 
 * @param length 
 */
void mesh_cmd_response(const char*text,uint8_t length)
{
    memcpy(uart_message,text,length);
    sprintf(uart_message+length,"\r\n");//Add line ending and NULL terminate it with sprintf
    ser_send(uart_message);
}


/**
 * @brief application rtc event which is a configurable period delay
 * through the uicr config "sleep" in the nodes databse
 * 
 */
void app_rtc_handler()
{
    uint32_t alive_count = mesh_tx_alive();//returns an incrementing counter
    
    NRF_LOG_INFO("id:%d:alive:%lu",get_this_node_id(),alive_count);
    #ifdef UART_SELF_ALIVE
        sprintf(rtc_message,"id:%d:alive:%lu;uart_rx:%lu\r\n",get_this_node_id(),alive_count,uart_rx_size);
        ser_send(rtc_message);
    #endif
    UNUSED_VARIABLE(alive_count);
}

int main(void)
{
    //MAKE SURE
    //UICR.NFCPINS = 0xFFFFFFFE - Disabled
    //UICR.REGOUT0 = 0xFFFFFFFD - 3.3 V

    uint32_t err_code;

    // ------------------------- Start Init ------------------------- 
    clocks_start();


    bsp_board_init(BSP_INIT_LEDS);

    nrf_gpio_cfg_output(GPIO_CUSTOM_Debug);
    nrf_gpio_pin_set(GPIO_CUSTOM_Debug);
    nrf_delay_ms(100);
    nrf_gpio_pin_clear(GPIO_CUSTOM_Debug);


    blink_red(100,200);
    blink_green(100,200);
    blink_blue(100,200);
    //ser_init(app_serial_handler);

    //Cannot use non-blocking with buffers from const code memory
    //reset is a status which single event is reset, that status takes the event name
    //sprintf(rtc_message,"nodeid:%d;channel:%d;reset:1\r\n",get_this_node_id(),mesh_channel());
    //ser_send(rtc_message);

    blink_red(500,500);

    err_code = mesh_init(rf_mesh_handler,mesh_cmd_response);
    APP_ERROR_CHECK(err_code);

    blink_green(500,500);
    //only allow interrupts to start after init is done
    rtc_config(app_rtc_handler);

    mesh_tx_reset();
    blink_blue(500,500);

    // ------------------------- Start Events ------------------------- 
    char rf_message[128];
    int count = 0;
    while(true)
    {
        nrf_gpio_pin_set(GPIO_CUSTOM_Debug);
        mesh_consume_rx_messages();
        nrf_gpio_pin_clear(GPIO_CUSTOM_Debug);
        blink_green(10,100);
        if(count%50 == 0)
        {
            sprintf(rf_message,"count:%d;debug:%lu",count,UICR_RF_CHANNEL);
            mesh_bcast_text(rf_message);
        }
        count++;
    }
}
/*lint -restore */
