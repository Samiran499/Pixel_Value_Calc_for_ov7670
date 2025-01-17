#include "Arduino.h"
#include "CameraOV7670.h"
#include "camera.h"
#include "drive.h"


const uint8_t VERSION            = 0x10;
const uint8_t COMMAND_NEW_FRAME  = 0x01 | VERSION;
const uint8_t COMMAND_DEBUG_DATA = 0x03 | VERSION;

const uint16_t UART_PIXEL_FORMAT_RGB565    = 0x01;
const uint16_t UART_PIXEL_FORMAT_GRAYSCALE = 0x02;

// Pixel byte parity check:
// Pixel Byte H: odd number of bits under H_BYTE_PARITY_CHECK and H_BYTE_PARITY_INVERT
// Pixel Byte L: even number of bits under L_BYTE_PARITY_CHECK and L_BYTE_PARITY_INVERT
//                                   H:RRRRRGGG
const uint8_t H_BYTE_PARITY_CHECK  = 0b00100000;
const uint8_t H_BYTE_PARITY_INVERT = 0b00001000;
//                                   L:GGGBBBBB
const uint8_t L_BYTE_PARITY_CHECK  = 0b00001000;
const uint8_t L_BYTE_PARITY_INVERT = 0b00100000;
// Since the parity for L byte can be zero we must ensure that the total byte value is above zero.
// Increasing the lowest bit of blue color is OK for that.
const uint8_t L_BYTE_PREVENT_ZERO = 0b00000001;

const uint16_t COLOR_GREEN = 0x07E0;
const uint16_t COLOR_RED   = 0xF800;


void processGrayscaleFrameBuffered();
void processGrayscaleFrameDirect();
void processRgbFrameBuffered();
void processRgbFrameDirect();

const uint16_t lineLength               = 160;
const uint16_t lineCount                = 120;
const uint32_t baud                     = 115200;
const uint16_t lineBufferLength         = lineLength;
const bool isSendWhileBuffering         = true;
const uint8_t uartPixelFormat           = UART_PIXEL_FORMAT_GRAYSCALE;
CameraOV7670 camera(CameraOV7670::RESOLUTION_QQVGA_160x120, CameraOV7670::PIXEL_YUV422, 17);

uint8_t lineBuffer[lineBufferLength]; // Two bytes per pixel
uint8_t* lineBufferSendByte;
bool isLineBufferSendHighByte;
bool isLineBufferByteFormatted;

uint16_t frameCounter                       = 0;
uint16_t processedByteCountDuringCameraRead = 0;


void commandStartNewFrame(uint8_t pixelFormat);
void commandDebugPrint(const String debugText);
uint8_t sendNextCommandByte(uint8_t checksum, uint8_t commandByte);


void sendBlankFrame(uint16_t color);
// could add __attribute__((always_inline)) to these
inline void processNextGrayscalePixelByteInBuffer();
inline void processNextRgbPixelByteInBuffer();
inline void tryToSendNextRgbPixelByteInBuffer();
inline void formatNextRgbPixelByteInBuffer();
inline uint8_t formatRgbPixelByteH(uint8_t byte);
inline uint8_t formatRgbPixelByteL(uint8_t byte);
inline uint8_t formatPixelByteGrayscaleFirst(uint8_t byte);
inline uint8_t formatPixelByteGrayscaleSecond(uint8_t byte);
inline void waitForPreviousUartByteToBeSent();
inline bool isUartReady();

// uint16_t frame_intensity = 0;
uint16_t column_intensitiy[5] = {0};
uint16_t pixelX = 0;
// uint16_t pixelY = 0;
float max = 0;
float mode = 0;
bool unexpected = false;
float reverse = 0;

// this is called in Arduino setup() function
void initializeScreenAndCamera()
{
    // Enable this for WAVGAT CPUs
    // For UART communiation we want to set WAVGAT Nano to 16Mhz to match Atmel based Arduino
    // CLKPR = 0x80; // enter clock rate change mode
    // CLKPR = 1; // set prescaler to 1. WAVGAT MCU has it 3 by default.
    Serial.begin(baud);
    if (camera.init()) { commandDebugPrint("Camera initialized."); }
    else { commandDebugPrint("Camera initialization failed."); }
    declarePins();
}


void sendBlankFrame(uint16_t color)
{
    uint8_t colorH = (color >> 8) & 0xFF;
    uint8_t colorL = color & 0xFF;

    commandStartNewFrame(UART_PIXEL_FORMAT_RGB565);
    for (uint16_t j = 0; j < lineCount; j++)
    {
        for (uint16_t i = 0; i < lineLength; i++)
        {
            waitForPreviousUartByteToBeSent();
            UDR0 = formatRgbPixelByteH(colorH);
            waitForPreviousUartByteToBeSent();
            UDR0 = formatRgbPixelByteL(colorL);
        }
    }
}


// this is called in Arduino loop() function
void processFrame()
{
    processedByteCountDuringCameraRead = 0;
    commandStartNewFrame(uartPixelFormat);
    noInterrupts();
    processGrayscaleFrameBuffered();
    interrupts();
    frameCounter++;
    commandDebugPrint("Frame " +
                      String(frameCounter) /* + " " + String(processedByteCountDuringCameraRead)*/);
    // commandDebugPrint("Frame " + String(frameCounter, 16)); // send number in hexadecimal
}


void processGrayscaleFrameBuffered()
{
    // frame_intensity = 0;
    uint16_t pixelX = 0;
    unexpected = false;
    column_intensitiy[0] = 0;
    column_intensitiy[1] = 0;
    column_intensitiy[2] = 0;
    column_intensitiy[3] = 0;
    column_intensitiy[4] = 0;
    // uint16_t pixelY = 0;
    camera.waitForVsync();
    commandDebugPrint("Vsync");

    camera.ignoreVerticalPadding();
    max = 0;
    mode = 0;

    for (uint16_t y = 0; y < lineCount/2; y++)
    {
        // pixelY = y;
        lineBufferSendByte = &lineBuffer[0];
        camera.ignoreHorizontalPaddingLeft();

        uint16_t x = 0;
        while (x < lineBufferLength)
        {
            pixelX = x;
            camera.waitForPixelClockRisingEdge(); // YUV422 grayscale byte
            camera.readPixelByte(lineBuffer[x]);
            lineBuffer[x] = formatPixelByteGrayscaleFirst(lineBuffer[x]);

            camera.waitForPixelClockRisingEdge(); // YUV422 color byte. Ignore.
            if (isSendWhileBuffering) { processNextGrayscalePixelByteInBuffer(); }
            // if(y >= (lineCount/2) - 3 && y <= (lineCount/2) + 4){
            //     if(x >= (lineBufferLength/2) - 3 && x <= (lineBufferLength/2) + 4){
            //         frame_intensity += lineBuffer[x];
            //     }
            // }
            
            if(y%4 == 0 && y <= lineCount/2) {
                column_intensitiy[pixelX/32] += lineBuffer[x]; // Accumulate the intensity
                // Don't ask any GenAI for this, they'll mess up
                // Also here's a high possibilty of losing some of the pixel values
                // So, keep that in mind
            }
            x++;

            camera.waitForPixelClockRisingEdge(); // YUV422 grayscale byte
            camera.readPixelByte(lineBuffer[x]);
            lineBuffer[x] = formatPixelByteGrayscaleSecond(lineBuffer[x]);

            camera.waitForPixelClockRisingEdge(); // YUV422 color byte. Ignore.
            if (isSendWhileBuffering) { processNextGrayscalePixelByteInBuffer(); }
            // if(y >= (lineCount/2) - 3 && y <= (lineCount/2) + 4){
            //     if(x >= (lineBufferLength/2) - 3 && x <= (lineBufferLength/2) + 4){
            //         frame_intensity += lineBuffer[x]; //  More computationally intense
            //     }
            // }

            if(y%4 == 0 && y <= lineCount/2) {
                column_intensitiy[pixelX/32] += lineBuffer[x]; // Accumulate the intensity
                // Don't ask any GenAI for this, they'll mess up
                // Also here's a high possibilty of losing some of the pixel values
                // So, keep that in mind
            }

            x++;
            // if(pixelX%10 == 0 && pixelY%10 == 0) {
            //     commandDebugPrint(String(box_intensitiy[(pixelY/10)*16 + (pixelX/10)]));
            // }
        }
        camera.ignoreHorizontalPaddingRight();

        // Debug info to get some feedback how mutch data was processed during line read.
        processedByteCountDuringCameraRead = lineBufferSendByte - (&lineBuffer[0]);

        // Send rest of the line
        while (lineBufferSendByte < &lineBuffer[lineLength])
        {
            processNextGrayscalePixelByteInBuffer();
        }

    };

    for(uint16_t i = 0; i < 5; i++) {
        column_intensitiy[i] = column_intensitiy[i]/600;
        commandDebugPrint(String(column_intensitiy[i]));
        max += (i + 1)*column_intensitiy[i];
        mode += column_intensitiy[i];
    }

    mode =  max/mode;

    if(mode > int(mode) + 0.5) {
        mode = int(mode) + 1;
    } else {
        mode = int(mode);
    }

    for(uint16_t j = 0; j < 4; j++) {
        if(column_intensitiy[j] == column_intensitiy[j + 1]) unexpected = true; 
    }

    commandDebugPrint(String(mode));
    commandDebugPrint(String(unexpected));

    drive(mode);
    
    // if(!unexpected) {
    //     drive(mode);
    // } else {
    //     drive(reverse);
    // }
    // commandDebugPrint(String(frame_intensity));
    // for(int i = 0; i < 8; i++) {
    //     commandDebugPrint(String(column_intensitiy[i]));
    // }
}

void processNextGrayscalePixelByteInBuffer()
{
    if (isUartReady())
    {
        UDR0 = *lineBufferSendByte;
        lineBufferSendByte++;
    }
}


void processGrayscaleFrameDirect()
{
    camera.waitForVsync();
    commandDebugPrint("Vsync");

    camera.ignoreVerticalPadding();

    for (uint16_t y = 0; y < lineCount; y++)
    {
        camera.ignoreHorizontalPaddingLeft();

        uint16_t x = 0;
        while (x < lineLength)
        {
            camera.waitForPixelClockRisingEdge(); // YUV422 grayscale byte
            camera.readPixelByte(lineBuffer[0]);
            lineBuffer[0] = formatPixelByteGrayscaleFirst(lineBuffer[0]);

            camera.waitForPixelClockRisingEdge(); // YUV422 color byte. Ignore.
            waitForPreviousUartByteToBeSent();
            UDR0 = lineBuffer[0];
            x++;

            camera.waitForPixelClockRisingEdge(); // YUV422 grayscale byte
            camera.readPixelByte(lineBuffer[0]);
            lineBuffer[0] = formatPixelByteGrayscaleSecond(lineBuffer[0]);

            camera.waitForPixelClockRisingEdge(); // YUV422 color byte. Ignore.
            waitForPreviousUartByteToBeSent();
            UDR0 = lineBuffer[0];
            x++;
        }

        camera.ignoreHorizontalPaddingRight();
    }
}

uint8_t formatPixelByteGrayscaleFirst(uint8_t pixelByte)
{
    // For the First byte in the parity chek byte pair the last bit is always 0.
    pixelByte &= 0b11111110;
    if (pixelByte == 0)
    {
        // Make pixel color always slightly above 0 since zero is a command marker.
        pixelByte |= 0b00000010;
    }
    return pixelByte;
}

uint8_t formatPixelByteGrayscaleSecond(uint8_t pixelByte)
{
    // For the second byte in the parity chek byte pair the last bit is always 1.
    return pixelByte | 0b00000001;
}


void processRgbFrameBuffered()
{
    camera.waitForVsync();
    commandDebugPrint("Vsync");

    camera.ignoreVerticalPadding();

    for (uint16_t y = 0; y < lineCount; y++)
    {
        lineBufferSendByte        = &lineBuffer[0];
        isLineBufferSendHighByte  = true; // Line starts with High byte
        isLineBufferByteFormatted = false;

        camera.ignoreHorizontalPaddingLeft();

        for (uint16_t x = 0; x < lineBufferLength; x++)
        {
            camera.waitForPixelClockRisingEdge();
            camera.readPixelByte(lineBuffer[x]);
            if (isSendWhileBuffering) { processNextRgbPixelByteInBuffer(); }
        };

        camera.ignoreHorizontalPaddingRight();

        // Debug info to get some feedback how mutch data was processed during line read.
        processedByteCountDuringCameraRead = lineBufferSendByte - (&lineBuffer[0]);

        // send rest of the line
        while (lineBufferSendByte < &lineBuffer[lineLength * 2])
        {
            processNextRgbPixelByteInBuffer();
        }
    }
}

void processNextRgbPixelByteInBuffer()
{
    // Format pixel bytes and send out in different cycles.
    // There is not enough time to do both on faster frame rates.
    if (isLineBufferByteFormatted) { tryToSendNextRgbPixelByteInBuffer(); }
    else { formatNextRgbPixelByteInBuffer(); }
}

void tryToSendNextRgbPixelByteInBuffer()
{
    if (isUartReady())
    {
        UDR0 = *lineBufferSendByte;
        lineBufferSendByte++;
        isLineBufferByteFormatted = false;
    }
}

void formatNextRgbPixelByteInBuffer()
{
    if (isLineBufferSendHighByte)
    {
        *lineBufferSendByte = formatRgbPixelByteH(*lineBufferSendByte);
    }
    else { *lineBufferSendByte = formatRgbPixelByteL(*lineBufferSendByte); }
    isLineBufferByteFormatted = true;
    isLineBufferSendHighByte  = !isLineBufferSendHighByte;
}


void processRgbFrameDirect()
{
    camera.waitForVsync();
    commandDebugPrint("Vsync");

    camera.ignoreVerticalPadding();

    for (uint16_t y = 0; y < lineCount; y++)
    {
        camera.ignoreHorizontalPaddingLeft();

        for (uint16_t x = 0; x < lineLength; x++)
        {

            camera.waitForPixelClockRisingEdge();
            camera.readPixelByte(lineBuffer[0]);
            lineBuffer[0] = formatRgbPixelByteH(lineBuffer[0]);
            waitForPreviousUartByteToBeSent();
            UDR0 = lineBuffer[0];

            camera.waitForPixelClockRisingEdge();
            camera.readPixelByte(lineBuffer[0]);
            lineBuffer[0] = formatRgbPixelByteL(lineBuffer[0]);
            waitForPreviousUartByteToBeSent();
            UDR0 = lineBuffer[0];
        }

        camera.ignoreHorizontalPaddingRight();
    };
}


// RRRRRGGG
uint8_t formatRgbPixelByteH(uint8_t pixelByteH)
{
    // Make sure that
    // A: pixel color always slightly above 0 since zero is end of line marker
    // B: odd number of bits for H byte under H_BYTE_PARITY_CHECK and H_BYTE_PARITY_INVERT to enable
    // error correction
    if (pixelByteH & H_BYTE_PARITY_CHECK) { return pixelByteH & (~H_BYTE_PARITY_INVERT); }
    else { return pixelByteH | H_BYTE_PARITY_INVERT; }
}


// GGGBBBBB
uint8_t formatRgbPixelByteL(uint8_t pixelByteL)
{
    // Make sure that
    // A: pixel color always slightly above 0 since zero is end of line marker
    // B: even number of bits for L byte under L_BYTE_PARITY_CHECK and L_BYTE_PARITY_INVERT to
    // enable error correction
    if (pixelByteL & L_BYTE_PARITY_CHECK)
    {
        return pixelByteL | L_BYTE_PARITY_INVERT | L_BYTE_PREVENT_ZERO;
    }
    else { return (pixelByteL & (~L_BYTE_PARITY_INVERT)) | L_BYTE_PREVENT_ZERO; }
}


void commandStartNewFrame(uint8_t pixelFormat)
{
    waitForPreviousUartByteToBeSent();
    UDR0 = 0x00; // New command

    waitForPreviousUartByteToBeSent();
    UDR0 = 4; // Command length

    uint8_t checksum = 0;
    checksum         = sendNextCommandByte(checksum, COMMAND_NEW_FRAME);
    checksum = sendNextCommandByte(checksum, lineLength & 0xFF); // lower 8 bits of image width
    checksum = sendNextCommandByte(checksum, lineCount & 0xFF);  // lower 8 bits of image height
    checksum = sendNextCommandByte(checksum,
                                   ((lineLength >> 8) & 0x03)      // higher 2 bits of image width
                                       | ((lineCount >> 6) & 0x0C) // higher 2 bits of image height
                                       | ((pixelFormat << 4) & 0xF0));

    waitForPreviousUartByteToBeSent();
    UDR0 = checksum;
}


void commandDebugPrint(const String debugText)
{
    if (debugText.length() > 0)
    {

        waitForPreviousUartByteToBeSent();
        UDR0 = 0x00; // New commnad

        waitForPreviousUartByteToBeSent();
        UDR0 = debugText.length() + 1; // Command length. +1 for command code.

        uint8_t checksum = 0;
        checksum         = sendNextCommandByte(checksum, COMMAND_DEBUG_DATA);
        for (uint16_t i = 0; i < debugText.length(); i++)
        {
            checksum = sendNextCommandByte(checksum, debugText[i]);
        }

        waitForPreviousUartByteToBeSent();
        UDR0 = checksum;
    }
}


uint8_t sendNextCommandByte(uint8_t checksum, uint8_t commandByte)
{
    waitForPreviousUartByteToBeSent();
    UDR0 = commandByte;
    return checksum ^ commandByte;
}


void waitForPreviousUartByteToBeSent()
{
    while (!isUartReady()); // wait for byte to transmit
}


bool isUartReady() { return UCSR0A & (1 << UDRE0); }
