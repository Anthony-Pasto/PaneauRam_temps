#include "PidLog.h"
#include <iomanip>

PidLog::PidLog()
    : debut(std::chrono::steady_clock::now())
{
    fichierGros.open("PID_gros_bassin.csv", std::ios::out | std::ios::trunc);
    fichierPetit.open("PID_petit_bassin.csv", std::ios::out | std::ios::trunc);
    fichierTemperature.open("PID_temperature.csv", std::ios::out | std::ios::trunc);

    if (fichierGros.is_open()) {
        fichierGros << "temps_s,niveau_gros_pct,consigne_gb_pct,commande_valve_gb_pct,gb_p,gb_i,gb_d\n";
        fichierGros << std::fixed << std::setprecision(6);
    }
    if (fichierPetit.is_open()) {
        fichierPetit << "temps_s,niveau_petit_pct,consigne_pb_pct,commande_valve_pb_pct,pb_p,pb_i,pb_d\n";
        fichierPetit << std::fixed << std::setprecision(6);
    }
    if (fichierTemperature.is_open()) {
        fichierTemperature << "temps_s,temperature_petit_C,consigne_tmp_pb_C,commande_valve_hot_pct,commande_valve_cold_pct,tmp_p,tmp_i,tmp_d\n";
        fichierTemperature << std::fixed << std::setprecision(6);
    }
}

PidLog::~PidLog()
{
    close();
}

bool PidLog::isOpen() const
{
    return fichierGros.is_open() && fichierPetit.is_open() && fichierTemperature.is_open();
}

void PidLog::enregistrer(
    double niveauGrosPct,
    double consigneGrosPct,
    double niveauPetitPct,
    double consignePetitPct,
    double temperaturePetitC,
    double consigneTemperaturePetitC,
    double commandeValveGrosPct,
    double commandeValvePetitPct,
    double commandeValveHotPct,
    double commandeValveColdPct,
    double gbP, double gbI, double gbD,
    double pbP, double pbI, double pbD,
    double tmpP, double tmpI, double tmpD)
{
    auto maintenant = std::chrono::steady_clock::now();
    double temps = std::chrono::duration<double>(maintenant - debut).count();

    if (fichierGros.is_open()) {
        fichierGros << temps << ',' << niveauGrosPct << ',' << consigneGrosPct << ','
                    << commandeValveGrosPct << ',' << gbP << ',' << gbI << ',' << gbD << '\n';
        fichierGros.flush();
    }

    if (fichierPetit.is_open()) {
        fichierPetit << temps << ',' << niveauPetitPct << ',' << consignePetitPct << ','
                     << commandeValvePetitPct << ',' << pbP << ',' << pbI << ',' << pbD << '\n';
        fichierPetit.flush();
    }

    if (fichierTemperature.is_open()) {
        fichierTemperature << temps << ',' << temperaturePetitC << ',' << consigneTemperaturePetitC << ','
                           << commandeValveHotPct << ',' << commandeValveColdPct << ','
                           << tmpP << ',' << tmpI << ',' << tmpD << '\n';
        fichierTemperature.flush();
    }
}

void PidLog::close()
{
    if (fichierGros.is_open()) fichierGros.close();
    if (fichierPetit.is_open()) fichierPetit.close();
    if (fichierTemperature.is_open()) fichierTemperature.close();
}