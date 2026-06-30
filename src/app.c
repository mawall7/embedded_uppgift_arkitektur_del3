#include "app.h"
#include "config.h"
#include "gpio.h"
#include "pins.h"
#include "uart.h"
#include "millis.h"
#include "spi.h"
#include "buzzer.h"
#include "servo.h"
#include "mfrc522.h"
#include "74hc595.h"
#include "keypad.h"
#include "ds1307.h"
#include "twi.h"
#include "led.h"
#include "access_protocol.h"
#include <string.h>
#include <util/delay.h>
#include <avr/interrupt.h>


/* STATE MACHINE*/

typedef enum {
    IDLE = 0,
    SENT_UID,
    AWAIT_PIN,
    SEND_PIN,
    SENT_PIN,
    PASSAGE_GRANTED,
    PASSAGE_DENIED,
    SERVER_NO_RESPONSE,
    SYSTEM_ERROR
} passage_system_t;

/* HELPER FUNCTIONS*/

void update_yellowblink(millis_t *start_blink);
void green_flash(millis_t *start_blink, bool *greenblinkstart, bool *greenblinkend);
void blink_blue(); 
void blink_red();

static void print_uid(const mfrc522_uid_t *uid)
{
    uint8_t i;
    uart_write_string("UID: ");
    for (i = 0; i < uid->size; i++)
    {
        uart_put_hex8(uid->uid[i]);
        if (i + 1 < uid->size)
        {
            uart_write_char(' ');
        }
    }
    uart_newline();
}

static void convert_uid(const mfrc522_uid_t *uid, char *uid_buff)
{
    static const char hex[] = "0123456789ABCDEF";

    uint8_t i;

    for (i = 0; i < uid->size; i++)
    {
        uint8_t value = uid->uid[i];

        *uid_buff++ = hex[(value >> 4) & 0x0F];
        *uid_buff++ = hex[value & 0x0F];
    }

    *uid_buff = '\0';
}


/* MAIN LOGIC LOOP*/


void app_init(void)
{
    millis_init();

    uart_init(UART_BAUDRATE);

    spi_init();
    
    servo_init();
    servo_close();
    buzzer_init();

    mfrc522_init();

    shift_register_init();

    keypad_init();

    twi_init(100000);

    init_led();

    access_init();

    sei();
    
}

void app_run(void)
{
//     rgb_off();
// red_led_on();
// _delay_ms(2000);

// rgb_off();
// green_led_on();
// _delay_ms(2000);

// rgb_off();
// yellow_led_on();
// _delay_ms(2000);

    // uint8_t version;
    // version = mfrc522_get_version();
    
    //red_led_on();
    
    DateTime current;

     
    passage_system_t STATE = IDLE;
    access_message_t msg;
    access_status_t s;
    uint16_t sid = 0;
    millis_t uidresp_time;
    millis_t pin_start;
    millis_t passage_time; 
    millis_t sentpin_millis;
    millis_t now = millis_get();
    millis_t greenledt;
    millis_t yellowledt;
    bool pintimeexceeded = false;
    bool pinentered = false;
    char keys[5] = {0};
    
    int d = 0;
    char k = '0';
    bool greenblinkstarted = false;
    bool greenblinkend = false;
    bool init_pinaw = false;
    bool entered = false;
    bool idle_init = false;
    while (1)
    {   
                
        switch (STATE)
        {
        case IDLE:
            if(!idle_init){
                
                rgb_off();
                red_led_on();
                mfrc522_init();
                idle_init = true;
            } 
        
            uint8_t atqa[2];
            uint8_t atqa_len;
            mfrc522_uid_t uid;
        
            if (mfrc522_request_a(atqa, &atqa_len) == MFRC522_OK)
            {
               
                if (mfrc522_anticoll_select(&uid) == MFRC522_OK )
                {
        
                    char uid_buff[21];
                    
                    convert_uid(&uid, uid_buff); 
                    s = access_send_uid(uid_buff);

        
                    if (s == ACCESS_STATUS_BUFFER_TOO_SMALL)
                    {
                        STATE = SYSTEM_ERROR;
                    }

                    if (s == ACCESS_STATUS_OK)
                    {
                        convert_uid(&uid, uid_buff); 
                        uidresp_time = millis_get();
                        STATE = SENT_UID;
                    }
                }
                
            }
            break;

        case SENT_UID:
            //uint16_t sid;
            s = access_read_message(&msg); 
        
            if ((millis_t)(millis_get() - uidresp_time) > 5000){ STATE = SERVER_NO_RESPONSE; break;}
            
            if (s == ACCESS_STATUS_OK)
            {   

                if(msg.command == ACCESS_CMD_REQ_PIN)
                {
                    sid = msg.sid;
                    rgb_off();
                    STATE = AWAIT_PIN;
                }
                else if (msg.command == ACCESS_CMD_TIMEOUT)
                {

                    STATE = SERVER_NO_RESPONSE; 
                }
                
            }
            break;

        case AWAIT_PIN:
                              
                if(!init_pinaw){
                    rgb_off();
                    yellowledt = millis_get();
                    pin_start = millis_get();
                    greenblinkstarted = false;
                    greenblinkend = false;
                    init_pinaw = true;
                }
                               
                  
                    if((millis_t)(millis_get() - pin_start) >= 10000){ STATE = IDLE; idle_init = false; init_pinaw = false; }
                    
                    if(greenblinkstarted == false && greenblinkend == false){
                        update_yellowblink(&yellowledt);
                    }
                                        
                    green_flash(&greenledt, &greenblinkstarted, &greenblinkend);
                                             
                    k = keypad_get_key_debounced();
                    
                    if(k >='0' && k <= '9' && greenblinkend == false && d < 4)
                    {
                       greenblinkstarted = true;
                       
                       keys[d] = k;
                       d++;
                       if(d == 4){
                        greenblinkstarted = false; 
                        greenblinkend = false; 
                        keys[4] = '\0';
                        d = 0;
                        STATE = SEND_PIN;
                        init_pinaw = false;
                    }    
                    
                }
            break;
        
        case SEND_PIN:
        {
              s = access_send_pin(sid, keys);
      
              sentpin_millis = millis_get();
                STATE = SENT_PIN;
            break;
        }
            
        case SENT_PIN:
        {   
                s = access_read_message(&msg);
      
                if((millis_t)(millis_get() - sentpin_millis) > 5000){
                    
                    STATE = SERVER_NO_RESPONSE; break;

                }
      
                if(s == ACCESS_STATUS_OK){
                    if(msg.command == ACCESS_CMD_OK)
                    {
                        
                        passage_time = millis_get();
                        STATE = PASSAGE_GRANTED;
                    
                        
                    }
                    else if (msg.command == ACCESS_CMD_TIMEOUT)
                    {
                        STATE = SERVER_NO_RESPONSE;
                    }
                    else if (msg.command == ACCESS_CMD_ERR)
                    {
                        STATE = PASSAGE_DENIED;
                    }
                    else if(msg.command == ACCESS_CMD_NACK){
      
                        STATE = IDLE;
                    }
                }
            }
            break; 

        case PASSAGE_GRANTED:
            if(!entered){  
                rgb_off();     
                green_led_on();
                buzzer_scream();
                servo_open();
                entered = true;
            }
            if ((millis_t)(millis_get() - passage_time) > 5000)
            {
                buzzer_quiet();
                servo_close();
                entered = false;
                idle_init = false;
                STATE = IDLE;
            }
            break;

        case PASSAGE_DENIED:
            blink_red();
            
            idle_init = false;
            STATE = IDLE;
            break;

        case SERVER_NO_RESPONSE:
            servo_close();        
            buzzer_quiet();
            blink_blue();
            idle_init = false;
            STATE = IDLE;
            break;   
        
        case SYSTEM_ERROR:
            magenta_led_on;
            _delay_ms(250);
            idle_init = false;
            STATE = IDLE;
            break;         

        default:
            break;
        }

       

 
    }
    
}

void green_flash(millis_t *start_blink, bool *greenblinkstart, bool *greenblinkend){

    
        millis_t millis_current = millis_get(); 
        
        if(*greenblinkstart){
            rgb_off();//yellow_led_off();
            green_led_on(); 
            *greenblinkstart = false;
            *greenblinkend = true;
            *start_blink = millis_get();
        }
        if(*greenblinkend && !*greenblinkstart && ((millis_t)(millis_current - *start_blink)) >=100){
            rgb_off();
            *greenblinkend = false;
            *greenblinkstart = false;
        }
        
    }

    void update_yellowblink(millis_t *start_blink){

                
        millis_t millis_current = millis_get();
        
        if(((millis_t)(millis_current - *start_blink) >= 250)){
            yellow_toggle();
            *start_blink = millis_get();   
        }
    }

    void blink_blue(){
        
        millis_t start = millis_get();
        millis_t current = start;
        rgb_off();
        
        do{
            if((millis_t)(millis_get() - current >= 500)){
                blue_toggle();
                current = millis_get();
            }

        }while((millis_t)(millis_get() - start) <= 3000);
        
    }

     void blink_red(){
        
        millis_t start = millis_get();
        millis_t current = start;
        rgb_off();
        
        do{
            if((millis_t)(millis_get() - current >= 500)){
                red_toggle();
                current = millis_get();
            }

        }while((millis_t)(millis_get() - start) <= 3000);
    }

