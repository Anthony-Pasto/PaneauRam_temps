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

    RequeteElkor requeteActuelle;

    unsigned char requete[8];
    unsigned char reponse[9];

    void construireRequete(RequeteElkor registre);
    bool envoyerRequete();
    bool recevoirReponse();
    bool verifierReponse();

    float convertirFloat();

    void modifierPartage(float valeur);
    void prochaineRequete();

public:

    PowerMeter(SerialPort& port);
    ~PowerMeter();

    void update();

    PowerMeterReading getReading();
};

#endif