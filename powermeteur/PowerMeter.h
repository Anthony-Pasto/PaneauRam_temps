#ifndef POWERMETER_H
#define POWERMETER_H

#include "Elkor.h"
#include "SerialPort.h"
#include <mutex>
#include <cstdint>

// Identification des requêtes Modbus
enum class RequeteElkor : uint16_t
{
    VAN = ADD_REG_VOLTAGE_A_N,
    VBN = ADD_REG_VOLTAGE_B_N,
    VAB = ADD_REG_VOLTAGE_A_B,
    IA  = ADD_REG_COURANT_A,
    IB  = ADD_REG_COURANT_B,
    KW  = ADD_REG_TOTAL_REAL_POWER,
    FP  = ADD_POWER_FACTOR_A,
    KWh = ADD_REG_TOTAL_ENERGY_CONSUMPTION
};

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

class PowerMeter
{
private:

    SerialPort& serial;
    std::mutex mutex;

    PowerMeterReading partage;

    const bool motsInverses;
    bool lireRegistre(RequeteElkor registre, float& valeur);

public:

    explicit PowerMeter(SerialPort& port, bool motsInverses = false);

    // Appeler depuis le thread d'acquisition. Publie un relevé complet.
    void update();

    PowerMeterReading getReading();
};

#endif
