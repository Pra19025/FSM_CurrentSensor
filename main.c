/* FILE:   FSM_GSM


    Envia un mensaje por medio de modulo
    GSM mediante el uso de una maquina de estados
    Finitos.

*/
#include "driverlib.h"
#include "device.h"
#include <stdio.h>
#include <string.h>


//---------------- prototipos de funciones --------------
void UartConfig();
__interrupt void RxHandler();
void gsmStartUp();
void gsmSend(char* mychar,uint16_t length, uint16_t try,uint16_t flag2See,uint16_t timeOut);
//--------------- Variables globales -------------------
// 2: ok recibido.
volatile uint16_t fsmGsmState = 0;
/*
 * 0: esperando una respuesta
 * 1: se recibio o
 * ...
 * 10: envio de mensaje
 * ...
 * 15: envio de mensaje correcto
 * ...
 * 20: se recibio una S
 * ...
 * 26: señal de buena calidad
 * ...
 * 30: se recibio R
 * ...
 * 36: registrado en la red.
 * 40: S
 * 41: SA
 * 42: SAP
 * 43: SAPB
 * 44: SAPBR
 * 45: SAPBR:
 * 46: SAPBR:' '
 * 47: SAPBR: 1
 * 48: SAPBR: 1, (si se recibi un uno luego, se reinicia)
 * ...
 * 50: D
 * 51: DO
 * 52: DOW
 * 53: DOWN
 * 54: DOWNL
 * 55: DOWNLO
 * 56: DOWNLOA (si el sigiente es D, encender bit)
 * ...
 * 60: +H
 * ...
 * 72: +HTTPACTION: 1
 * ...
 * 76: +HTTPACTION: 1,200
 * */
volatile uint16_t flags_rx = 0x0000;
/**
 *     bit 0 = gsm responde (1 si, 0 no)
 *     bit 1 = espera de mensaje a enviar
 *     bit 2 = mensaje enviado con exito
 *     bit 3 = Hay señal.
 *     bit 4 = registrado en red
 *     bit 5 = SMS bit
 *     bit 6 = SAPBR ok
 *     bit 7 = data to send (HTTP)
 *     bit 8 = HTTP ACTION ok (codigo 200)
 *     bit 9 = ERROR bit
 */
#define OK_BIT 0
#define SEND_BIT 1
#define OKSEND_BIT 2
#define OKCSQ_BIT 3
#define OKCREG_BIT 4
#define SMS_BIT 5 //se desea enviar sms o no
#define SAPBR_BIT 6
#define DOWNLOAD_BIT 7
#define CODE200_BIT 8
#define ERROR_BIT 9
uint16_t mainFsm = 0;
/*
 * 0: estado inicial
 * 1: verificar señal
 * 2:
 * ...
 * 10: GSM se encuentra activo, colocar en modo texto el modulo
 * 11: Se coloca el numero a enviar
 * 12: Se escribe el mensje y envia
 * 13: fin envio, aqui se queda
 * ...
 * 20: Establecer conexión gprs
 * 21: Establecer APN
 * 22: Iniciar la conexion IP
 * 23: Verificar conexión IP
 * 24: Iniciar HTTP
 * 25: Establecer el CID
 * 26: Establecer URL
 * 27: Indicar largo de datos y timeout
 * 28: enviar los datos
 * 29: enviar el paquete, por metodo POST
 * */

//--------- VARIABLES DE PRUEBA -----------------
uint16_t contData = 0;
char *out;
uint16_t dataLenght=0;


void main(void){
#ifdef _FLASH
    memcpy(&RamfuncsRunStart, &RamfuncsLoadStart, (size_t)&RamfuncsLoadSize);
#endif
    // configurar el reloj, 50Mhz, con el oscilador interno
    while(!SysCtl_setClock(SYSCTL_PLL_ENABLE | SYSCTL_SYSDIV(1) | SYSCTL_IMULT(10)|
                            SYSCTL_OSCSRC_OSC2)){
        SysCtl_resetMCD();
    }

    // ---- GPIO config ----------
    GPIO_setPinConfig(GPIO_4_GPIO4);
    GPIO_setDirectionMode(4, GPIO_DIR_MODE_OUT);
    // ---- configuraciones de interrupciones
    Interrupt_initModule();
    Interrupt_initVectorTable();
    UartConfig();
    // habilitar interrupciones.
    EINT;
    ERTM;
    // señal de inicio modulo gsm
    //gsmStartUp(); // señal de inicio

    while(1){
        switch (mainFsm){
            case 0:
                //mandar a encender el modulo

                // confirmar que este activo el modulo
                gsmSend("AT\r\n",4, 10, OK_BIT,2);

                // si se levanta la bandera entonces
                // se cambia al estado 2.
                if (flags_rx & (1<<OK_BIT)){
                    mainFsm = 1;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }
                break;

            case 1:
                //comprobar señal
                gsmSend("AT+CSQ\r\n", 8 , 10, OKCSQ_BIT,2);

                if(flags_rx & (1<<OKCSQ_BIT) ){
                    mainFsm = 2;
                    flags_rx &= ~(1<<OKCSQ_BIT);
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }
                break;
            case 2:
                //comprobar registro en red
                //comprobar señal
                gsmSend("AT+CREG?\r\n", 10 , 10, OKCREG_BIT,2);
                // si esta en 1 el bit sms envia el mensaje,
                if(flags_rx & (1<<OKCREG_BIT) && flags_rx & (1<<SMS_BIT) ){
                    mainFsm = 10;
                    flags_rx &= ~(1<<OKCREG_BIT);
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }
                // si no se quiere enviar un mensaje
                else if( flags_rx & (1<<OKCREG_BIT) && !(flags_rx & (1<<SMS_BIT)) ){
                    mainFsm = 20;
                    flags_rx &= ~(1<<OKCREG_BIT);
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok

                }
                break;

            case 10:
                // fsm activa, se configura en modo texto
                gsmSend("AT+CMGF=1\r\n", 11 , 5, OK_BIT,2);
                // se procede a mandar el mensaje
                if (flags_rx &(1<< OK_BIT)){
                    mainFsm = 11;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }
                //levantar servicios IP
                break;
            case 11:
                // enviar el sms
                flags_rx &= ~(1<<SEND_BIT);
                gsmSend("AT+CMGS=\"+50254605224\"\r\n", 24 , 1, SEND_BIT,2);
                DEVICE_DELAY_US(1000000);
                // si es 0 se sale, de lo contrario se procede a
                // escribir el mensaje.
                if (fsmGsmState&(1<<SEND_BIT)) mainFsm = 12;
                break;

            case 12:
                // escribir el mensaje
                gsmSend("Mensajes desde el TMS320F0023C\x1a", 31, 1, OKSEND_BIT,2);
                DEVICE_DELAY_US(5000000);
                if(flags_rx &(1<<OKSEND_BIT)) mainFsm =13;

                //levatar servicio HTTP
                break;
            case 13:
                break;

            case 20:
                gsmSend("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"\r\n", 31, 2, OK_BIT,2);
                if(flags_rx &(1<<OK_BIT)){
                    mainFsm =21;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }
                break;
            case 21:
                gsmSend("AT+SAPBR=3,1,\"APN\",\"internet.ideasclaro\"\r\n", 42, 2, OK_BIT,2);
                if(flags_rx &(1<<OK_BIT)){
                    mainFsm =22;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }
                break;
            case 22:
                gsmSend("AT+SAPBR=1,1\r\n", 14, 2, OK_BIT,2);
                if(flags_rx &(1<<OK_BIT)){
                    mainFsm =23;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }else if(flags_rx &(1<<ERROR_BIT)){
                    mainFsm =31;
                    flags_rx &= ~(1<<ERROR_BIT); //apagar el bit ok
                }
                break;
            case 23:
                gsmSend("AT+SAPBR=2,1\r\n", 14, 2, SAPBR_BIT,2);
                if(flags_rx &(1<<SAPBR_BIT) && flags_rx &(1<<OK_BIT)  ){
                    mainFsm =24;
                    flags_rx &= ~(1<<SAPBR_BIT); //apagar el bit ok
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok

                }else   mainFsm =20;
                break;

            case 24:
                // se inicia la comunicacion http
                gsmSend("AT+HTTPINIT\r\n", 13, 2, OK_BIT,2);
                if(flags_rx &(1<<OK_BIT)){
                    mainFsm =25;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }else if(flags_rx &(1<<ERROR_BIT)){
                    mainFsm =30;
                    flags_rx &= ~(1<<ERROR_BIT); //apagar el bit ok
                }
                break;
            case 25:
                // se coloca el perfil 1,
                gsmSend("AT+HTTPPARA=\"CID\",1\r\n", 21, 2, OK_BIT,2);
                if(flags_rx &(1<<OK_BIT)){
                    mainFsm =26;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }
                break;
            case 26:
                //establecer la url
                gsmSend("AT+HTTPPARA=\"URL\",\"api.thingspeak.com/update\"\r\n", 47, 2, OK_BIT,2);
                if(flags_rx &(1<<OK_BIT)){
                    mainFsm =27;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }
                break;

            case 27:
                //se indica el alrgo de los datos a enviar
                gsmSend("AT+HTTPDATA=34,10000\r\n", 22, 2, DOWNLOAD_BIT,2);
                if(flags_rx &(1<<DOWNLOAD_BIT)){
                    mainFsm =28;
                    flags_rx &= ~(1<<DOWNLOAD_BIT); //apagar el bit ok
                }
                break;
            case 28:
                //se cargan los datos
                sprintf(out, "api_key=DXYYKWOLHFHBRAIB&field1=%d\r\n",contData);
                dataLenght = strlen(out);
                gsmSend(out, dataLenght+2, 1, OK_BIT,10);
                if(flags_rx &(1<<OK_BIT)){
                    mainFsm =29;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                    //se aumenta el contador
                    contData++;
                    if(contData>=20) contData = 0; // se reinicia
                }else mainFsm = 27;
                break;

            case 29:
                //se indica el alrgo de los datos a enviar
                gsmSend("AT+HTTPACTION=1\r\n", 17, 2, CODE200_BIT,100);
                if(flags_rx &(1<<CODE200_BIT)){
                    flags_rx &= ~(1<<CODE200_BIT); //apagar el bit ok
                    mainFsm =30;
                }


                break;
            case 30:
                gsmSend("AT+HTTPTERM\r\n", 13, 2, OK_BIT,20);
                if(flags_rx &(1<<OK_BIT)){
                    mainFsm = 24;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }else if(flags_rx &(1<<ERROR_BIT)){
                    mainFsm = 20; //se vuelven a iniciar los servicios IP
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }
                DEVICE_DELAY_US(10000000); //esperar 10 segundos
                break;
            case 31:
                gsmSend("AT+SAPBR=0,1\r\n", 14, 2, OK_BIT,20);
                if(flags_rx &(1<<OK_BIT)){
                    mainFsm = 20;
                    flags_rx &= ~(1<<OK_BIT); //apagar el bit ok
                }

            default:
                break;

        }
        //delay 100ms
        DEVICE_DELAY_US(100000);
    }

}


/*
 * Envia el mensaje especificado, hasta que se coloque en 1
 * la bandera especificada, o bien se agoten los intentos
 *
 * mychar: mensaje a enviar por uart
 * length: largo de la cadena de caracteres
 * delay: tiempo de espera en ms entre intento e intento, removido
 * try: cantidad de intentos que se realizarán
 * flag2See: numero de bit a revisar para comprobar que se haya
 *           ejecutado bien el comando.
 *
 * */
void gsmSend(char* mychar,uint16_t length, uint16_t try,uint16_t flag2See,uint16_t timeOut){
    uint16_t counter = 0x00;
    flags_rx &= ~(1<<flag2See);
    uint16_t i;
    while(!(flags_rx&1<<flag2See) && counter<try){
        // se manda la cadaena de caracteres
        SCI_writeCharArray(SCIA_BASE,(uint16_t *) mychar, length);
        // se espera la respuesta
        counter++;
        for(i=0; i<timeOut;i++) DEVICE_DELAY_US(50000); //esperar
    }

    return;
}
//--------------- GSM funciones --------------------

void gsmStartUp(){
    // power on gsm
    GPIO_writePin(4, 0);
    DEVICE_DELAY_US(1000000);
    GPIO_writePin(4, 1);
    DEVICE_DELAY_US(2000000);
    GPIO_writePin(4, 0);
    DEVICE_DELAY_US(3000000);
}


//---------------- UART funciones ------------------

void UartConfig(){

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_SCIA);
    //CONF UART GPIO
    GPIO_setPinConfig(GPIO_2_SCIA_TX);
    GPIO_setDirectionMode(2, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(2, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(2, GPIO_QUAL_ASYNC);

    GPIO_setPinConfig(GPIO_3_SCIA_RX);
    GPIO_setDirectionMode(3, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(3, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(3, GPIO_QUAL_ASYNC);
    // SCI module configuration
    SCI_performSoftwareReset(SCIA_BASE); // reset SCIA
    SCI_setConfig(SCIA_BASE, SysCtl_getClock(10000000)/4, 57600, (SCI_CONFIG_WLEN_8 |
                                                        SCI_CONFIG_STOP_ONE |
                                                        SCI_CONFIG_PAR_NONE));
    SCI_resetChannels(SCIA_BASE);
    SCI_resetRxFIFO(SCIA_BASE); // limpiar FIFO Rx
    SCI_resetTxFIFO(SCIA_BASE); // limpiar FIFO Tx
    SCI_clearInterruptStatus(SCIA_BASE, SCI_INT_TXFF | SCI_INT_RXFF); // reiniciar las banderas de interrupciones
    SCI_enableFIFO(SCIA_BASE);
    SCI_enableModule(SCIA_BASE);
    SCI_performSoftwareReset(SCIA_BASE);
    SCI_enableInterrupt(SCIA_BASE, SCI_INT_RXFF ); //habilitar interrupcion de recepcion
    SCI_setFIFOInterruptLevel(SCIA_BASE, SCI_FIFO_TX16, SCI_FIFO_RX1);
    /*---------INTERRUPT------------------*/
    Interrupt_register(INT_SCIA_RX, &RxHandler);
    Interrupt_enable(INT_SCIA_RX);


    return;
}

__interrupt
void RxHandler(){
    uint16_t data;
    SCI_readCharArray(SCIA_BASE,  &data , 1);
    SCI_clearOverflowStatus(SCIA_BASE);
    SCI_clearInterruptStatus(SCIA_BASE, SCI_INT_RXFF);

}


