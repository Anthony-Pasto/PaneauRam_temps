#ifndef BALANCE_H
#define BALANCE_H


#include "SerialPort.h"
#include <cstdint>
#include <array>



// Structure de partage des données
struct PowerMeterReading
{
    float VAN = 0;
    float VBN = 0;
    float VAB = 0;
    float IA  = 0;
    float IB  = 0;
    float KW  = 0;
    float FP  = 0;
    float KWh = 0;

    bool connexion = false;
};

class Balance
{
private:

    SerialPort& serial;
    PowerMeterReading partage;

    const bool motsInverses;
   

    bool lireRegistre(std::array<unsigned char, 18> &trame);


    unsigned char trame[18];

public:

    explicit PowerMeter(SerialPort& port, bool motsInverses = false);

    // Appeler depuis le thread d'acquisition. Publie un relevé complet.
    void update();

    PowerMeterReading getReading();
     bool motsInverses;
};

#endif
