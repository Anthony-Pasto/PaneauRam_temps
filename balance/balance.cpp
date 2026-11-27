#include "balance.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace
{
    constexpr unsigned char lectureRegistres = 0x03;
    constexpr unsigned char octetsParMesure = 4;
    constexpr int delaiReponseMs = 200;
    constexpr int silenceTrameMs = 5; // Pour un port configuré à 9600 bauds.

}

Balance::Balance(SerialPort &port, bool motsInverses)
    : serial(port), motsInverses(motsInverses)
{
}

bool Balance::lireRegistre(std::array<unsigned char, 18> &trame)
{
    while (true)
    {
        int n = serial.readBytes(&trame[0], 1, 1000);

        if (n < 0)
        {
            return false;
        }

        if (n != 1 || trame[0] != 0x02)
        {
            continue;
        }

        // Lecture des 17 octets restants
        n = serial.readBytes(&trame[1], 17, 100);

        if (n < 0)
        {
            return false;
        }

        if (n != 17)
        {
            continue;
        }

        // Vérification du CR
        if (trame[16] != 0x0D)
        {
            continue;
        }

        return true;
    }
}

void Balance::update()
{
   
   
}

PowerMeterReading Balance::getReading()
{
  
    
}
