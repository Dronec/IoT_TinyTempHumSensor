#ifndef Helpers_h_
#define Helpers_h_
#include <ESP8266WiFi.h>

class Helpers
{
public:
    static void Sleep();
    static int SerialPrintln(const char *format);
    static int SerialPrintf(const char *format, ...);
    static void Readlog();
    static void Uploadlog(WiFiClient &client);
    static void Blink(int num, int on, int off);
};
#endif // Helpers_h_