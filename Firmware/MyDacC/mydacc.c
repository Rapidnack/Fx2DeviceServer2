#pragma NOIV               // Do not generate interrupt vectors

#include "fx2.h"
#include "fx2regs.h"
#include "syncdly.h"            // SYNCDELAY macro

extern BOOL GotSUD;             // Received setup data flag
extern BOOL Sleep;
extern BOOL Rwuen;
extern BOOL Selfpwr;

BYTE Configuration;             // Current configuration
BYTE AlternateSetting;          // Alternate settings

#define VENDOR_DEVICE_TYPE 0xC0
#define VENDOR_DEVICE_PARAM 0xC1
#define SET_SAMPLE_RATE 0xC2

#define DEVICE_TYPE 0x03
#define DATA_PORT_NO 52003
#define CONTROL_PORT_NO 53003

//-----------------------------------------------------------------------------
// Task Dispatcher hooks
//   The following hooks are called by the task dispatcher.
//-----------------------------------------------------------------------------

BYTE StateCycle[] = { 11, 1, 11 }; // sample rate : 48MHz / (11 + 1 + 11 + 1) = 2MHz

unsigned long GetRate()
{
   BYTE i;
   unsigned int cycle = 0;

   for ( i = 0; i < sizeof StateCycle; i++ )
   {
      cycle += StateCycle[i] ? StateCycle[i] : 256;
   }
   cycle++; // jump to S0
   return 48000000UL / cycle;
}

void ChangeStateCycle()
{
   char xdata WaveData[128];
   BYTE i;

   AUTOPTRH1 = MSB( &WaveData );
   AUTOPTRL1 = LSB( &WaveData );  
   AUTOPTRH2 = 0xE4;
   AUTOPTRL2 = 0x00;
   for ( i = 0x00; i < 128; i++ )
   {
      EXTAUTODAT1 = EXTAUTODAT2;
   }

   for ( i = 0; i < sizeof StateCycle; i++ )
   {
      WaveData[64 + i] = StateCycle[i];
      WaveData[96 + i] = StateCycle[i];
   }

   AUTOPTRH1 = MSB( &WaveData );
   AUTOPTRL1 = LSB( &WaveData );  
   AUTOPTRH2 = 0xE4;
   AUTOPTRL2 = 0x00;
   for ( i = 0x00; i < 128; i++ )
   {
      EXTAUTODAT2 = EXTAUTODAT1;
   }
}

void GpifInit( void );

void TD_Init(void)             // Called once at startup
{
   CPUCS = ((CPUCS & ~bmCLKSPD) | bmCLKSPD1) ;	// 48 MHz CPU clock

   REVCTL = 0x03; // REVCTL.0 and REVCTL.1 set to 1
   SYNCDELAY;

   EP2CFG = 0xA8; // OUT, Bulk, 1024, Quad

   GpifInit();
   ChangeStateCycle();

   // start GPIF FIFO Write
   FIFORESET = 0x80; // activate NAK-ALL to avoid race conditions
   SYNCDELAY;

   EP2FIFOCFG = 0x00; //switching to manual mode
   SYNCDELAY;
   FIFORESET = 0x02; // Reset FIFO 2
   SYNCDELAY;
   OUTPKTEND = 0X82; //OUTPKTEND done four times as EP2 is quad buffered
   SYNCDELAY;
   OUTPKTEND = 0X82;
   SYNCDELAY;
   OUTPKTEND = 0X82;
   SYNCDELAY;
   OUTPKTEND = 0X82;
   SYNCDELAY;
   EP2FIFOCFG = 0x10; //switching to auto mode
   SYNCDELAY;

   FIFORESET = 0x00; //Release NAKALL
   SYNCDELAY;

   EP2GPIFFLGSEL = 1;  // GPIF FIFOFlag is empty
   SYNCDELAY;

	GPIFTCB0 = 1;
	SYNCDELAY;
   // trigger FIFO write transactions, using SFR
   GPIFTRIG = 0 | 0; // R/W=0, EP[1:0]=FIFO_EpNum
}

void TD_Poll(void)              // Called repeatedly while the device is idle
{
}

BOOL TD_Suspend(void)          // Called before the device goes into suspend mode
{
   return(TRUE);
}

BOOL TD_Resume(void)          // Called after the device resumes
{
   return(TRUE);
}

//-----------------------------------------------------------------------------
// Device Request hooks
//   The following hooks are called by the end point 0 device request parser.
//-----------------------------------------------------------------------------

BOOL DR_GetDescriptor(void)
{
   return(TRUE);
}

BOOL DR_SetConfiguration(void)   // Called when a Set Configuration command is received
{
   Configuration = SETUPDAT[2];
   return(TRUE);            // Handled by user code
}

BOOL DR_GetConfiguration(void)   // Called when a Get Configuration command is received
{
   EP0BUF[0] = Configuration;
   EP0BCH = 0;
   EP0BCL = 1;
   return(TRUE);            // Handled by user code
}

BOOL DR_SetInterface(void)       // Called when a Set Interface command is received
{
   AlternateSetting = SETUPDAT[2];
   return(TRUE);            // Handled by user code
}

BOOL DR_GetInterface(void)       // Called when a Set Interface command is received
{
   EP0BUF[0] = AlternateSetting;
   EP0BCH = 0;
   EP0BCL = 1;
   return(TRUE);            // Handled by user code
}

BOOL DR_GetStatus(void)
{
   return(TRUE);
}

BOOL DR_ClearFeature(void)
{
   return(TRUE);
}

BOOL DR_SetFeature(void)
{
   return(TRUE);
}

BOOL DR_VendorCmnd(void)
{
   switch (SETUPDAT[1])
   {
      case VENDOR_DEVICE_TYPE:
         EP0BUF[0] = DEVICE_TYPE;
         EP0BCH = 0;
         EP0BCL = 1;
         break;

      case VENDOR_DEVICE_PARAM:
         EP0BUF[0] = DATA_PORT_NO & 0xFF;
         EP0BUF[1] = (DATA_PORT_NO >> 8) & 0xFF;
         EP0BUF[2] = CONTROL_PORT_NO & 0xFF;
         EP0BUF[3] = (CONTROL_PORT_NO >> 8) & 0xFF;
         EP0BCH = 0;
         EP0BCL = 4;
         break;

      case SET_SAMPLE_RATE:
         {
            unsigned long rate;
            unsigned long cycles;

				rate = (unsigned long)SETUPDAT[2]
					 + ((unsigned long)SETUPDAT[3] << 8)
					 + ((unsigned long)SETUPDAT[4] << 16)
					 + ((unsigned long)SETUPDAT[5] << 24);
				if (rate == 0)
					rate = 1;
				cycles = 48000000 / rate;
				if (cycles < 4)
					cycles = 4; // 12MHz
				if (cycles > 500)
					cycles = 500; // 96kHz
				if (cycles % 2 == 0)
				{
	            StateCycle[0] = (cycles / 2) - 1;
	            StateCycle[2] = (cycles / 2) - 1;
				}
				else
				{
	            StateCycle[0] = (cycles / 2) - 1;
	            StateCycle[2] = (cycles / 2);
				}
            rate = GetRate();

            GPIFABORT = 0xFF; // abort any waveforms pending
            while( !( GPIFTRIG & 0x80 ) ) // poll GPIFTRIG.7 Done bit
               ;

            ChangeStateCycle();

   			GPIFTCB0 = 1;
   			SYNCDELAY;
            // trigger FIFO write transactions, using SFR
            GPIFTRIG = 0 | 0; // R/W=0, EP[1:0]=FIFO_EpNum

            EP0BUF[0] = rate & 0xFF;
            EP0BUF[1] = (rate >> 8) & 0xFF;
            EP0BUF[2] = (rate >> 16) & 0xFF;
            EP0BUF[3] = (rate >> 24) & 0xFF;
            EP0BCH = 0;
            EP0BCL = 4;
         }
         break;

      default:
         return(TRUE);
   }

   return(FALSE);
}

//-----------------------------------------------------------------------------
// USB Interrupt Handlers
//   The following functions are called by the USB interrupt jump table.
//-----------------------------------------------------------------------------

// Setup Data Available Interrupt Handler
void ISR_Sudav(void) interrupt 0
{
   GotSUD = TRUE;            // Set flag
   EZUSB_IRQ_CLEAR();
   USBIRQ = bmSUDAV;         // Clear SUDAV IRQ
}

// Setup Token Interrupt Handler
void ISR_Sutok(void) interrupt 0
{
   EZUSB_IRQ_CLEAR();
   USBIRQ = bmSUTOK;         // Clear SUTOK IRQ
}

void ISR_Sof(void) interrupt 0
{
   EZUSB_IRQ_CLEAR();
   USBIRQ = bmSOF;          	// Clear SOF IRQ
}

void ISR_Ures(void) interrupt 0
{
   // whenever we get a USB reset, we should revert to full speed mode
   pConfigDscr = pFullSpeedConfigDscr;
   ((CONFIGDSCR xdata *) pConfigDscr)->type = CONFIG_DSCR;
   pOtherConfigDscr = pHighSpeedConfigDscr;
   ((CONFIGDSCR xdata *) pOtherConfigDscr)->type = OTHERSPEED_DSCR;

   EZUSB_IRQ_CLEAR();
   USBIRQ = bmURES;         // Clear URES IRQ
}

void ISR_Susp(void) interrupt 0
{
   Sleep = TRUE;
   EZUSB_IRQ_CLEAR();
   USBIRQ = bmSUSP;
}

void ISR_Highspeed(void) interrupt 0
{
   if (EZUSB_HIGHSPEED())
   {
      pConfigDscr = pHighSpeedConfigDscr;
      ((CONFIGDSCR xdata *) pConfigDscr)->type = CONFIG_DSCR;
      pOtherConfigDscr = pFullSpeedConfigDscr;
      ((CONFIGDSCR xdata *) pOtherConfigDscr)->type = OTHERSPEED_DSCR;
   }

   EZUSB_IRQ_CLEAR();
   USBIRQ = bmHSGRANT;
}
void ISR_Ep0ack(void) interrupt 0
{
}
void ISR_Stub(void) interrupt 0
{
}
void ISR_Ep0in(void) interrupt 0
{
}
void ISR_Ep0out(void) interrupt 0
{
}
void ISR_Ep1in(void) interrupt 0
{
}
void ISR_Ep1out(void) interrupt 0
{
}
void ISR_Ep2inout(void) interrupt 0
{
}
void ISR_Ep4inout(void) interrupt 0
{
}
void ISR_Ep6inout(void) interrupt 0
{
}
void ISR_Ep8inout(void) interrupt 0
{
}
void ISR_Ibn(void) interrupt 0
{
}
void ISR_Ep0pingnak(void) interrupt 0
{
}
void ISR_Ep1pingnak(void) interrupt 0
{
}
void ISR_Ep2pingnak(void) interrupt 0
{
}
void ISR_Ep4pingnak(void) interrupt 0
{
}
void ISR_Ep6pingnak(void) interrupt 0
{
}
void ISR_Ep8pingnak(void) interrupt 0
{
}
void ISR_Errorlimit(void) interrupt 0
{
}
void ISR_Ep2piderror(void) interrupt 0
{
}
void ISR_Ep4piderror(void) interrupt 0
{
}
void ISR_Ep6piderror(void) interrupt 0
{
}
void ISR_Ep8piderror(void) interrupt 0
{
}
void ISR_Ep2pflag(void) interrupt 0
{
}
void ISR_Ep4pflag(void) interrupt 0
{
}
void ISR_Ep6pflag(void) interrupt 0
{
}
void ISR_Ep8pflag(void) interrupt 0
{
}
void ISR_Ep2eflag(void) interrupt 0
{
}
void ISR_Ep4eflag(void) interrupt 0
{
}
void ISR_Ep6eflag(void) interrupt 0
{
}
void ISR_Ep8eflag(void) interrupt 0
{
}
void ISR_Ep2fflag(void) interrupt 0
{
}
void ISR_Ep4fflag(void) interrupt 0
{
}
void ISR_Ep6fflag(void) interrupt 0
{
}
void ISR_Ep8fflag(void) interrupt 0
{
}
void ISR_GpifComplete(void) interrupt 0
{
}
void ISR_GpifWaveform(void) interrupt 0
{
}
