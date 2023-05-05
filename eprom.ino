//coop 29/03/2023
#include <Arduino.h>
#include <stdio.h>
#include "xmodem.h"
#include "config.h"
#include "md5stuff.h"

// command line
#define ECHO_ON

#define LOWADDRESSLATCH  A0
#define HIGHADDRESSLATCH A1
#define CE      A3
#define OE      A4
#define WE   A5

#define FLASHA16   10
#define FLASHA17   11
#define FLASHA18   12


//4040 stuff
#define COUNTERMASK 0x0fff

#define DS      A0
#define LATCH   A1
#define CLOCK   A2

#define PORTC_DS      0
#define PORTC_LATCH   1
#define PORTC_CLOCK   2

#define PORTC_CE      3
#define PORTC_OE      4
#define PORTC_WE      5


// eeprom stuff
// define the IO lines for the data - bus
#define D0 2
#define D1 3
#define D2 4
#define D3 5
#define D4 6
#define D5 7
#define D6 8
#define D7 9


//command buffer for parsing commands
#define COMMANDSIZE 32
char cmdbuf[COMMANDSIZE];

unsigned long startAddress, endAddress;
unsigned long  lineLength, dataLength;
int loop_cmd = 0; // set to >0 for prompt for command mode
int available;
int bytes;  // Make sure this is not defined in 'loop'!
int total;
unsigned long flashaddress;

//define COMMANDS
#define NOCOMMAND    0
#define VERSION      1
#define SET_ADDRESS  2
#define CLEAR_RAM    3

#define READ_HEX    10
#define READ_BIN    11
#define READ_ITL    12

#define WRITE_HEX   20

#define WRITE_BIN   21
#define WRITE_ITL   22
#define WRITE_XMODEM 23

#define FLASH_IDENTIFY 30
#define FLASH_ERASE 31

#define MD5SUM 40
#define BLANKCHECK 41
#define ADDEBUG 42


/*****************************************************************
 *
 *  CONTROL and DATA functions
 *
 ****************************************************************/

// switch IO lines of databus to INPUT state
#ifdef OLD_IO
void data_bus_input() {
   
  pinMode(D0, INPUT);
  pinMode(D1, INPUT);
  pinMode(D2, INPUT);
  pinMode(D3, INPUT);
  pinMode(D4, INPUT);
  pinMode(D5, INPUT);
  pinMode(D6, INPUT);
  pinMode(D7, INPUT);
}
#endif

#ifdef FAST_IO
void      data_bus_input() {
  DDRD = DDRD & 0x03; // leave bits 0 and 1 as is
  DDRB = DDRB & 0xfc; // leave bits 7 to 2 as is. 
}
#endif

#ifdef OLD_IO
//switch IO lines of databus to OUTPUT state
void data_bus_output() {
  pinMode(D0, OUTPUT);
  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);
  pinMode(D3, OUTPUT);
  pinMode(D4, OUTPUT);
  pinMode(D5, OUTPUT);
  pinMode(D6, OUTPUT);
  pinMode(D7, OUTPUT);
}
#endif

#ifdef FAST_IO
void data_bus_output() {
  DDRD = DDRD | 0xfc; // leave bits 0 and 1 as is
  DDRB = DDRB | 0x03; // leave bits 7 to 2 as is. 
}

#endif

#ifdef OLD_IO
//read a complete byte from the bus
//be sure to set data_bus to input before
byte read_data_bus()
{
  return ((digitalRead(D7) << 7) +
          (digitalRead(D6) << 6) +
          (digitalRead(D5) << 5) +
          (digitalRead(D4) << 4) +
          (digitalRead(D3) << 3) +
          (digitalRead(D2) << 2) +
          (digitalRead(D1) << 1) +
          digitalRead(D0));

}
#endif

#ifdef FAST_IO
byte read_data_bus()
{

  byte b = ((PIND & 0xfc) >> 2) | ((PINB & 0x03) << 6);
  return b;
  
}
#endif


#ifdef OLD_IO
//write a byte to the data bus
//be sure to set data_bus to output before
void write_data_bus(byte data)
{
  //2 bits belong to PORTB and have to be set separtely
  digitalWrite(D6, (data >> 6) & 0x01);
  digitalWrite(D7, (data >> 7) & 0x01);
  //bit 0 to 6 belong to bit 2 to 8 of PORTD
  // meeprommer used to just OR PIND ... which is wrong. You need to mask too
  PORTD = (PIND & 0x03) | ( data << 2 );
  //digitalWrite(D0, (data & 0x01));
  //digitalWrite(D1, (data >> 1) & 0x01);
  //digitalWrite(D2, (data >> 2) & 0x01);
  //digitalWrite(D3, (data >> 3) & 0x01);
  //digitalWrite(D4, (data >> 4) & 0x01);
  //digitalWrite(D5, (data >> 5) & 0x01);
  
  
}
#endif

#ifdef FAST_IO
void write_data_bus(byte data)
{
  PORTD = (PORTD & 0x03) | ( data << 2 );
  PORTB = (PORTB & 0xfc) | (data >> 6);  
}

#endif

#ifdef HC595

void set_address_bus(unsigned long address)
{

  //get bits 15 to 8 of address
  byte hi = ((address >>8) & 0xff);
  
  //get bits 7 to 0 of address
  byte low = address & 0xff;

  //disable latch line
  bitClear(PORTC,PORTC_LATCH);

  //shift out highbyte
  fastShiftOut(hi);
  //shift out lowbyte
  fastShiftOut(low);

  //enable latch and set address
  bitSet(PORTC,PORTC_LATCH);

  // Now the top 3 address bits
  digitalWrite(FLASHA16, (address >>16) & 0x01);
  digitalWrite(FLASHA17, (address >>17) & 0x01);
  digitalWrite(FLASHA18, (address >>18) & 0x01);
}

//faster shiftOut function then normal IDE function (about 4 times)
void fastShiftOut(byte data) {
  //clear data pin
  bitClear(PORTC,PORTC_DS);
  //Send each bit of the myDataOut byte MSBFIRST
  for (int i=7; i>=0; i--)  {
    bitClear(PORTC,PORTC_CLOCK);
    //--- Turn data on or off based on value of bit
    if ( bitRead(data,i) == 1) {
      bitSet(PORTC,PORTC_DS);
    }
    else {      
      bitClear(PORTC,PORTC_DS);
    }
    //register shifts bits on upstroke of clock pin  
    bitSet(PORTC,PORTC_CLOCK);
    //zero the data pin after shift to prevent bleed through
    bitClear(PORTC,PORTC_DS);
  }
  //stop shifting
  bitClear(PORTC,PORTC_CLOCK);
}
#endif // HC595



//short function to set the OE(output enable line of the eeprom)
// attention, this line is LOW - active
#ifdef OLD_IO
void set_oe (byte state)
{
  digitalWrite(OE, state);
}
#endif

#ifdef FAST_IO
void set_oe (byte state)
{
  if (state==LOW) {
     PORTC &= ~_BV(PORTC_OE);
  } else {
     PORTC |= _BV(PORTC_OE);
  }
}
#endif

#ifdef OLD_IO
//short function to set the CE(chip enable line of the eeprom)
// attention, this line is LOW - active
void set_ce (byte state)
{
  digitalWrite(CE, state);
}
#endif

#ifdef FAST_IO
void set_ce (byte state)
{
   if (state==LOW) {
     PORTC &= ~_BV(PORTC_CE);
  } else {
     PORTC |= _BV(PORTC_CE);
  }
}
#endif

#ifdef OLD_IO
//short function to set the WE(write enable line of the eeprom)
// attention, this line is LOW - active
void set_we (byte state)
{
  digitalWrite(WE, state);
}
#endif

#ifdef FAST_IO
void set_we (byte state)
{
   if (state==LOW) {
     PORTC &= ~_BV(PORTC_WE);
  } else {
     PORTC |= _BV(PORTC_WE);
  }
}
#endif


//highlevel function to read a byte from a current address of 4040 and then bump it up one
byte read_next_byte(void)
{
  byte      data = 0;

  //Serial.println("read_next_byte");
  set_address_bus(flashaddress);
  
  //set databus for reading
  data_bus_input();
  //first disable output
  set_oe(HIGH);
  //disable write
  set_we(HIGH);
  //enable chip select
  set_ce(LOW);
  //enable output
  set_oe(LOW);
  delayMicroseconds(1);

  data = read_data_bus();

  //disable output
  set_oe(HIGH);
  set_ce(HIGH);

  flashaddress++;

  return data;

}

byte eprom_write_next_byte(byte b)
{
 
  set_address_bus(flashaddress);

  //first disable output
  set_oe(HIGH);
  //set databus for writing
  data_bus_output();

  //disable write
  //set_we(HIGH);
  write_data_bus(b);
  delayMicroseconds(1); // you are meant to do 2 ... but the function call/exit will consume time
  //enable chip select
  set_ce(LOW);
  //enable output
  //set_oe(LOW);
  delayMicroseconds(98);
  //set_we(HIGH); // latch data
  set_ce(HIGH);
  delayMicroseconds(1);

  data_bus_input();

  flashaddress++;

}

/************************************************
 *
 * COMMAND and PARSING functions
 *
 *************************************************/

//waits for a string submitted via serial connection
//returns only if linebreak is sent or the buffer is filled
void readCommand() {
  //first clear command buffer
  for (int i = 0; i < COMMANDSIZE; i++) cmdbuf[i] = 0;
  //initialize variables
  char c = ' ';
  int idx = 0;
  //now read serial data until linebreak or buffer is full
  while (1) {
    if (idx >= COMMANDSIZE) {
      cmdbuf[idx - 1] = 0;
      break;
    }
    if (Serial.available()) {
      c = Serial.read();
      if ((c == '\n') || (c == '\r')) {
        cmdbuf[idx] = 0;
        break;
      }
      if (( c == 8 ) && (idx > 0)) { //delete backspace, clear on terminal 1 char
        idx--;
        //Serial.print(idx,DEC);
        Serial.write(8);
        Serial.write(' ');
        Serial.write(8);
      }
      if ((c>31) && (c<127)) {
#ifdef ECHO_ON            
      Serial.write(c); // echo it back
#endif
      cmdbuf[idx++] = c;
      }
      
    }
  }
}

//parse the given command by separating command character and parameters
//at the moment only 5 commands are supported
byte parseCommand() {
  //set ',' to '\0' terminator (command string has a fixed strucure)
  //first string is the command character
  cmdbuf[1]  = 0;
  //second string is startaddress (5 bytes)
  cmdbuf[7]  = 0;
  //third string is a data length (orig file said endaddress (5 bytes))
  cmdbuf[13] = 0;
  //fourth string is length (2 bytes)
  cmdbuf[16] = 0;
  startAddress = hexFiveWord((cmdbuf + 2));
  dataLength = hexFiveWord((cmdbuf + 8));
  lineLength = hexByte(cmdbuf + 14);
  byte retval = 0;
  switch (cmdbuf[0]) {
    case 'a':
      retval = SET_ADDRESS;
      break;
    case 'c':
      retval = CLEAR_RAM;
      break;
    case 'r':
      retval = READ_HEX;
      break;
    case 'w':
      retval = WRITE_XMODEM;
      break;
    case 't':  // write TWO bytes t xxxxx 0aabb
      retval = WRITE_HEX;
      break;  
    case 'i':
      retval = FLASH_IDENTIFY;
      break;
    case 'e':
      retval = FLASH_ERASE;
      break;
    case 'm':
      retval = MD5SUM;
      break;
    case 'b':
      retval = BLANKCHECK;
      break;
    case 'x':
      retval = ADDEBUG;
      break;
    default:
      retval = NOCOMMAND;
      break;
  }

  return retval;
}

/************************************************************
 * convert a single hex digit (0-9,a-f) to byte
 * @param char c single character (digit)
 * @return byte represented by the digit
 ************************************************************/
byte hexDigit(char c)
{
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  else if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  else if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  else {
    return 0;   // getting here is bad: it means the character was invalid
  }
}

/************************************************************
 * convert a hex byte (00 - ff) to byte
 * @param c-string with the hex value of the byte
 * @return byte represented by the digits
 ************************************************************/
byte hexByte(char* a)
{
  return ((hexDigit(a[0]) * 16) + hexDigit(a[1]));
}

/************************************************************
 * convert a hex word (0000 - ffff) to unsigned int
 * @param c-string with the hex value of the word
 * @return unsigned int represented by the digits
 ************************************************************/
unsigned int hexWord(char* data) {
  if (data[0]==0) {
     return 0;
  }
  return ((hexDigit(data[0]) * 4096) +
          (hexDigit(data[1]) * 256) +
          (hexDigit(data[2]) * 16) +
          (hexDigit(data[3])));
}

// 5 digit hex number
unsigned long hexFiveWord(char* data) {
  if (data[0]==0) {
     return 0;
  }
  return ((hexDigit(data[0])*65536L) +
          (hexDigit(data[1]) * 4096L) +
          (hexDigit(data[2]) * 256L) +
          (hexDigit(data[3]) * 16L) +
          (hexDigit(data[4])));
}
/************************************************
 *
 * INPUT / OUTPUT Functions
 *
 *************************************************/


/**
 * read a data block from eeprom and write out a hex dump
 * of the data to serial connection
 * @param from       start address to read fromm
 * @param to         last address to read from
 * @param linelength how many hex values are written in one line
 **/
void read_block(unsigned long  from, unsigned long  to, int linelength)
{
  flashaddress=from;
  set_address_bus(flashaddress);
  //count the number fo values that are already printed out on the
  //current line
  int        outcount = 0;
  //loop from "from address" to "to address" (included)
  for (unsigned long address = from; address <= to; address++) {
    if (outcount == 0) {
      //print out the address at the beginning of the line
      //Serial.println();
      Serial.print("\r\n");
      Serial.print("0x");
      printAddress(address);
      Serial.print(" : ");
    }
    //print data, separated by a space
    byte data = read_next_byte();
    printByte(data);
    Serial.print(" ");
    outcount = (++outcount % linelength);

  }
}  
//dcdcdcdc
void setaddress(unsigned long  from)
{
  set_address_bus(from);
      printAddress(from);
      Serial.print(" : ");
      set_address_bus(from);
//      byte data = read_next_byte();
//*    printByte(data);
     Serial.println(); 
}

void read_md5(unsigned long startAddress, unsigned long dataLength) {
  unsigned long  bytesleft;
  int bytestoprocess;
  int i;
  char md5buffer[64];
  char md5str[33];
  MD5_CTX context;
  unsigned char digest[16];
  
  flashaddress=startAddress;
  set_address_bus(flashaddress);

  md5str[0] = '\0';
  MD5Init(&context);
  
  while (flashaddress < (startAddress + dataLength) ) {
     // Work out how many bytes are left
     bytesleft = ((startAddress + dataLength) - flashaddress);
     bytestoprocess=64;
     if (bytesleft < 64) {
        bytestoprocess = bytesleft;
     }
     for (i=0;i<bytestoprocess;i++) {
        md5buffer[i] = (char) read_next_byte();
     }
     MD5Update(&context, md5buffer, bytestoprocess);
     // No need to bump up flash address as read_next_byte does it.
  }

  MD5Final(digest, &context);

  make_digest(md5str, digest, 16);
  Serial.println(md5str);
  Serial.println();
  
}

void blank_check(unsigned long startAddress, unsigned long dataLength) {
  byte b;
  boolean found_non_ff = false;
    
  flashaddress=startAddress;
  set_address_bus(flashaddress);
  
  while (flashaddress < (startAddress + dataLength) ) {
     b = read_next_byte();
     if (b != 0xff) {
         found_non_ff = true;
         flashaddress--;
         break;
     }
  }   
  if (found_non_ff) {
    Serial.print("Found byte ");
    printByte(b);
    Serial.print(" at ");
    printAddress(flashaddress);
    Serial.println();
  } else {
    Serial.println("Scan complete. Only FFs found");
  }
  
}

// debug function
void write_two_bytes(unsigned long from, byte a, byte b)
{
  // prep
  //special_flash_write(0x0000, 0xf0); // read-reset
  
  flashaddress=from;
  set_address_bus(flashaddress);
  
  eprom_write_next_byte(a);
  eprom_write_next_byte(b);
  
  //print a newline after the last data line
  Serial.println();
  Serial.println("Put Vcc back to 5V, and Vpp back to 5V (or G*Vpp back to Arduino A4");
}

/**
 * print out a 20 bit word as 5 character hex value
 **/
void printAddress(unsigned long address) {
  if (address < 0x0010) Serial.print("0");
  if (address < 0x0100) Serial.print("0");
  if (address < 0x1000) Serial.print("0");
  if (address < 0x10000) Serial.print("0");
  Serial.print(address, HEX);

}

/**
 * print out a byte as 2 character hex value
 **/
void printByte(byte data) {
  if (data < 0x10) Serial.print("0");
  Serial.print(data, HEX);
}

/************************************************
 *
 * MAIN
 *
 *************************************************/
void setup() {
//menu as a constant
static const char MMENU[] PROGMEM = "Started.\n\r"
  "  a nnnnn - Set address\n\r"
  "  r nnnnn mmmmm - show mmmmm bytes at address nnnnn\n\r"
  "  w nnnnn - write to eprom using xmodem transfer\n\r"
  "  m nnnnn mmmmm - md5sum rom content starting at nnnnn for mmmmmm bytes long\n\r"
  "  b nnnnn mmmmm - Check for FFs from nnnnn for mmmmmm bytes long\n\r";
  
#ifdef HC595
  pinMode(DS,OUTPUT);
  pinMode(LATCH,OUTPUT);
  pinMode(CLOCK,OUTPUT);
#endif

//  pinMode(LED_BUILTIN, OUTPUT);

  
  pinMode(CE, OUTPUT);
  digitalWrite(CE, HIGH);
  pinMode(WE, OUTPUT);
  digitalWrite(WE, HIGH);

  pinMode(OE, OUTPUT);
  digitalWrite(OE, HIGH);
  
  pinMode(FLASHA16, OUTPUT);
  pinMode(FLASHA17, OUTPUT);
  pinMode(FLASHA18, OUTPUT);

  xmodem_set_config(XMODEM_MODE_ORIGINAL);
  Serial.begin(115200);

 // Serial.println("Started.");
 // Serial.println("  r nnnnn mmmmm - show mmmmm bytes at address nnnnn");
 // Serial.println("  w nnnnn - write to eprom using xmodem transfer");
 // Serial.println("  m nnnnn mmmmm - md5sum rom content starting at nnnnn for mmmmmm bytes long");
 // Serial.println("  b nnnnn mmmmm - Check for FFs from nnnnn for mmmmmm bytes long");
 // Serial.println("  x nnnnn - Debug set address pins.");
  Serial.print((__FlashStringHelper*)MMENU);
}

/////////////////////////
static bool data_handler(const uint8_t* buffer, size_t size)
{
  for(int c=0; c<size; c++) {
    eprom_write_next_byte(buffer[c]);
  }
  return true;
}

static bool input_handler(int c)
{
//  if (c == 13)
//  {
//    change_settings();
//    return true;
//  }
  return false;
}

/**
 * main loop, that runs invinite times, parsing a given command and
 * executing the given read or write requestes.
 **/
void loop() {
  byte a, b;
  unsigned int c;

static const char MINST1[] PROGMEM = "Switch to PGM on the board, then\n\r"
  "start an xmodem transfer in your terminal ...\n\r";
static const char MINST2[] PROGMEM = "\n\rTransfer finished. switch back to NORM before any other command.\n\r";

//  if (loop_cmd == NOCOMMAND) {
    Serial.print("\r% "); // show the prompt
    readCommand();
#ifdef ECHO_ON    
    Serial.println(""); // only required in ECHO mode
#endif    
    byte cmd = parseCommand();
    
    switch (cmd) {

      // setting the address like this is mainly used for testing to see if your 74HC595s or 74LS374s are working.
      case SET_ADDRESS:
        // Set the address bus to an arbitrary value.
        // Useful for debugging shift-register wiring, byte-order.
        // e.g. a,00FF
        Serial.print("Setting address bus to 0x");
        Serial.println(cmdbuf + 2);
        Serial.println(startAddress);
        set_address_bus(startAddress);

        break;
 
         
      case READ_HEX:
        //set a default if needed to prevent infinite loop
        if (lineLength == 0) lineLength = 16;
        if (dataLength == 0) {
           dataLength = 0x80;
        }
        endAddress = startAddress + dataLength - 1;
        read_block(startAddress, endAddress, lineLength);
        break;
      case MD5SUM:
        //set a default if needed to prevent infinite loop
        
        if (dataLength == 0) {
           dataLength = 0x80;
        }
        endAddress = startAddress + dataLength - 1;
        Serial.println("Reading md5");
        read_md5(startAddress, dataLength);
        break;
     case BLANKCHECK:
        //set a default if needed to prevent infinite loop
        
        if (dataLength == 0) {
           dataLength = 0x80;
        }
        endAddress = startAddress + dataLength - 1;
        Serial.println("Checking for anything that isnt an FF");
        blank_check(startAddress, dataLength);
        break;

      case READ_ITL:
        break;

      case WRITE_HEX: // t xxxxx 0yyzz  Write at xxxxx the two bytes yy and zz
        // simple test write of two bytes at the address
        a = ((dataLength >> 8) & 0xff);
        b = dataLength & 0xff;
        write_two_bytes(startAddress, a, b);
        break;
        
      case ADDEBUG:
         setaddress(startAddress);
         break;
         
      case WRITE_XMODEM:
 //       Serial.println("Please start an xmodem transfer in your terminal ... ");
 //       Serial.println("when the transfer finishes remove the Vpp voltage and switch Vcc back to 5V.");
 //       Serial.println("If its a 27C512 you will also need to connect OE* back to the arduino)");
        bytes = 0;
        total=0;
        flashaddress=startAddress;
        set_address_bus(startAddress);

        Serial.print((__FlashStringHelper*)MINST1);
        int32_t sizeReceived = xmodem_receive(NULL, input_handler, data_handler);

//printf("Transfer complete - %ld bytes written to EEPROM\n\r", sizeReceived);
        delay(2000);
        while (Serial.available()) Serial.read();
        Serial.print((__FlashStringHelper*)MINST2);
        printAddress(sizeReceived); Serial.print(" bytes written\n\r");
        break;
      default:
        break;
    }
//  }
}
