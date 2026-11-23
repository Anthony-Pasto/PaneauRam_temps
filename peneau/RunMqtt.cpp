#include "RunMqtt.h"
#include "Mqtt.h"
#include "RunPID.h"
#include "Panneau.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <sstream>

RunMqtt::RunMqtt(const RunPID &runPID, Mqtt &mqtt, Panel &panneau)
    : Thread("RunMqtt"), runPID(runPID), mqtt(mqtt), panneau(panneau)
{
}

RunMqtt::~RunMqtt()
{
    stop();
    join();
}

void RunMqtt::configurerConsignes(double petit, double temperature)
{
    std::lock_guard<std::mutex> verrou(mutexConsignes);
    consignePetit = petit;
    consigneTemperature = temperature;
}

void RunMqtt::task()
{
    using Horloge = std::chrono::steady_clock;
    const std::chrono::milliseconds periode(200);

    while (enMarche)
    {
        const auto prochainCycle = Horloge::now() + periode;
        Mqtt::Message message;
        if (mqtt.recevoir(message))
        {
            std::string valeur = message.contenu;
            std::istringstream topic(message.topic);
            std::string RAM, DOMAIN, CMD, capteur;

            std::getline(topic, RAM, '/');
            std::getline(topic, DOMAIN, '/');
            std::getline(topic, CMD, '/');
            std::getline(topic, capteur, '/');
            if (RAM != "RAM" || (CMD != "CMD" && CMD != "ACK"))
            {
                break;
            }
            if (DOMAIN == "PANNEAU")
            {
                if (capteur == "NivGB")
                {
                    double gros = std::stod(valeur);
                    // std::lock_guard<std::mutex> verrou(mutexConsignes);
                    // consigneGros = gros;
                }
                else if (capteur == "NivPB")
                {
                    double petit = std::stod(valeur);
                    // std::lock_guard<std::mutex> verrou(mutexConsignes);
                    // consignePetit = petit;
                }
                else if (capteur == "TmpGB")
                {
                    double temperature = std::stod(valeur);
                    // std::lock_guard<std::mutex> verrou(mutexConsignes);
                    // consigneTemperature = temperature;
                }
                else if (capteur == "TmpPB")
                {
                    double temperature = std::stod(valeur);
                    // std::lock_guard<std::mutex> verrou(mutexConsignes);
                    // consigneTemperature = temperature;
                }
                else if (capteur == "ValveGB")
                {
                    double petit = std::stod(valeur);
                    std::lock_guard<std::mutex> verrou(mutexConsignes); // a meui developper et pas forcement bon
                    consignePetit = petit;
                }
                else if (capteur == "ConsTempPB")
                {
                    double temperature = std::stod(valeur);
                    std::lock_guard<std::mutex> verrou(mutexConsignes);
                    temperatureRecue = temperature;
                    nouvelleConsigneTemperature = true;
                }
            }
            // mqtt.envoyer("RAM/PANNEAU/CMD/test", message.contenu);
        }
        const auto resultat = runPID.getResultat();
        if (resultat.disponible)
        {
            double petit;
            double temperature;
            {
                std::lock_guard<std::mutex> verrou(mutexConsignes);
                petit = consignePetit;
                temperature = consigneTemperature;
            }

            const PanelReading &lecture = resultat.lecture;
            mqtt.envoyer("RAM/PANNEAU/CMD/ConsTmpPB", petit);
            mqtt.envoyer("RAM/PANNEAU/CMD/ConsTempPB", temperature);
            mqtt.envoyer("RAM/PANNEAU/ETATS/NivGB", lecture.niveauGrosBassin);
            mqtt.envoyer("RAM/PANNEAU/ETATS/NivPB", lecture.niveauPetitBassin);
            mqtt.envoyer("RAM/PANNEAU/ETATS/TmpGB", lecture.temperatureGrosBassin);
            mqtt.envoyer("RAM/PANNEAU/ETATS/TmpPB", lecture.temperaturePetitBassin);
            mqtt.envoyer("RAM/PANNEAU/ETATS/ValveEC", lecture.ouvertureValve[static_cast<int>(Valve::HotWater)]);
            mqtt.envoyer("RAM/PANNEAU/ETATS/ValveGB", lecture.ouvertureValve[static_cast<int>(Valve::LargeTank)]);
            mqtt.envoyer("RAM/PANNEAU/ETATS/ValvePB", lecture.ouvertureValve[static_cast<int>(Valve::SmallTank)]);
            mqtt.envoyer("RAM/PANNEAU/ETATS/ValveEF", lecture.ouvertureValve[static_cast<int>(Valve::ColdWater)]);
            mqtt.envoyer("RAM/PANNEAU/ETATS/ValveEEC", panneau.isHotWaterOn() ? "ON" : "OFF");
            mqtt.envoyer("RAM/PANNEAU/ETATS/ValveEEF", panneau.isColdWaterOn() ? "ON" : "OFF");
            // mqtt.envoyer("RAM/POWERMETER/ETATS/Ia", lecture.courant[0]); pas vrai valeur
            // mqtt.envoyer("RAM/POWERMETER/ETATS/Ib", lecture.courant[1]);
            mqtt.envoyer("RAM/ALARMES/ETATS/ALR_GB_OVF", lecture.debordementGrosBassin ? "ON" : "OFF");
            mqtt.envoyer("RAM/ALARMES/ETATS/ALR_PB_OVF", lecture.debordementPetitBassin ? "ON" : "OFF");
            mqtt.envoyer("RAM/PANNEAU/ETATS/Mode", resultat.automatique ? "AUTO" : "MANUAL");
            mqtt.envoyer("RAM/PANNEAU/ETATS/Pompe", panneau.isPumpOn() ? "ON" : "OFF");
        }

        std::this_thread::sleep_until(prochainCycle);
    }
}
bool RunMqtt::prendreConsigneTemperature(double& valeur)
{
    std::lock_guard<std::mutex> verrou(mutexConsignes);
    if (!nouvelleConsigneTemperature)
        return false;

    valeur = temperatureRecue;
    nouvelleConsigneTemperature = false;
    return true;
}