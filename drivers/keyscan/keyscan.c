#include <stdio.h>
#include "uart.h"
#include <string.h>
#include <ctype.h>
#include "keypad.h"
#include "millis.h"

static int code_count;
static int n_correct;

char code_correct[] = "2435"; 

void screen_init(){
    code_count = 0;
    n_correct = 0;
}

int screen(millis_t now)
{
    
    char key = keypad_get_key_debounced();

    if(key!=0){

        if (key == code_correct[code_count++])
        {
            n_correct++;
        }
        if(code_count == 4){
            if(n_correct == 4){
                screen_init();
                return 1;
            }
            screen_init();
            return -1;
        }
        
        return 0;
    }
    //no key press 
    return -2;
    
}
