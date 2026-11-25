#include "RunMqtt.h"
#include "Mqtt.h"



#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>
#include <sstream>
#include <utility>

RunMqtt::RunMqtt( Mqtt &mqtt)
    : Thread("RunMqtt"), mqtt(mqtt)
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
        for (int i = 0; i < 32 && enMarche && mqtt.recevoir(message); ++i)
        {
            std::istringstream topic(message.topic);
            std::string reseau, domaine, categorie, capteur;
            if (!std::getline(topic, reseau, '/') ||
                !std::getline(topic, domaine, '/') ||
                !std::getline(topic, categorie, '/') ||
                !std::getline(topic, capteur, '/') ||
                topic.peek() != std::char_traits<char>::eof() ||
                reseau != "RAM")
                continue;

            using Alarme = RunPID::Alarme;
            Alarme alarme = Alarme::Nombre;
            if (capteur == "ALR_GB_OVF")
                alarme = Alarme::DebordementGros;
            else if (capteur == "ALR_PB_OVF")
                alarme = Alarme::DebordementPetit;
            else if (capteur == "ALR_GB_NIV_MAX")
                alarme = Alarme::NiveauMaxGros;
            else if (capteur == "ALR_PB_NIV_MAX")
                alarme = Alarme::NiveauMaxPetit;
            else if (capteur == "ALR_CNX_BAL")
                alarme = Alarme::ConnexionBalance;
            else if (capteur == "ALR_CNX_POW")
                alarme = Alarme::ConnexionPowerMeter;

            if (alarme != Alarme::Nombre)
            {
                if (domaine == "ALARMES" && categorie == "ACK" &&
                    alarme != Alarme::NiveauMaxGros && alarme != Alarme::NiveauMaxPetit)
                {
                    std::lock_guard<std::mutex> verrou(mutexConsignes);
                    if (runPID.getResultat().arretDebordement)
                    {
                        enAttente = MisesAJour{};
                        misesAJourDisponibles = false;
                    }
                    runPID.acquitterAlarme(alarme);
                }
                else if (domaine == "ALARMES" && categorie == "BRUT" &&
                         alarme != Alarme::DebordementGros && alarme != Alarme::DebordementPetit &&
                         alarme != Alarme::NiveauMaxGros && alarme != Alarme::NiveauMaxPetit &&
                         (message.contenu == "ON" || message.contenu == "OFF"))
                {
                    runPID.actualiserAlarme(alarme, message.contenu == "ON");
                }
                continue;
            }

            if ((domaine != "PANNEAU" && domaine != "ALARMES" ) || categorie != "CMD")
                continue;

            const auto etat = runPID.getResultat();
            if (etat.arretDebordement || etat.arretSerie)
                continue;

            ValeurRecue MisesAJour::*champ = nullptr;
            if (capteur == "ConsTmpPB" || capteur == "ConsTempPB")
                champ = &MisesAJour::consigneTemperature;
            else if (capteur == "Mode")
                champ = &MisesAJour::mode;
            else if (capteur == "Pompe")
                champ = &MisesAJour::pompe;
            else if (capteur == "ValveEC")
                champ = &MisesAJour::valveEC;
            else if (capteur == "ValveG" || capteur == "ValveGB")
                champ = &MisesAJour::valveGB;
            else if (capteur == "ValvePB")
                champ = &MisesAJour::valvePB;
            else if (capteur == "ValveEF")
                champ = &MisesAJour::valveEF;
            else if (capteur == "ValveEEC")
                champ = &MisesAJour::valveEEC;
            else if (capteur == "ConsNivGB")
                champ = &MisesAJour::consigneGros;
            else if (capteur == "ConsNivPB")
                champ = &MisesAJour::consignePetit;
            else if (capteur == "ValveEEF")
                champ = &MisesAJour::valveEEF;
            else if (capteur == "NivGB")
                champ = &MisesAJour::nivGB;
            else if (capteur == "NivPB")
                champ = &MisesAJour::nivPB;
            else if (capteur == "TmpGB")
                champ = &MisesAJour::tmpGB;
            else if (capteur == "TmpPB")
                champ = &MisesAJour::tmpPB;
            else if (capteur == "NivLhGB")
                champ = &MisesAJour::hauterMaxGB;
            else if (capteur == "TgNivGB")
                champ = &MisesAJour::tempsActiveAlarmeGB;
            else if (capteur == "TrNivGB")
                champ = &MisesAJour::tempsIgnorerGB;
            else if (capteur == "TemperatureActive")
                champ = &MisesAJour::temperatureActive;
            else if (capteur == "NivLhPB")
                champ = &MisesAJour::hauterMaxPB;
            else if (capteur == "TgNivPB")
                champ = &MisesAJour::tempsActiveAlarmePB;
            else if (capteur == "TrNivPB")
                champ = &MisesAJour::tempsIgnorerPB;

            if (champ)
            {
                std::lock_guard<std::mutex> verrou(mutexConsignes);
                auto &valeur = enAttente.*champ;
                valeur.contenu = message.contenu;
                valeur.recue = true;
                misesAJourDisponibles = true;
            }
        }

        const auto resultat = runPID.getResultat();
        using Alarme = RunPID::Alarme;
        auto publierAlarme = [&](Alarme alarme, const char* topic) {
            const bool memorisee = resultat.alarmesMemorisees[static_cast<std::size_t>(alarme)];
            mqtt.envoyer(topic, memorisee ? "ON" : "OFF");
        };
        publierAlarme(Alarme::DebordementGros, "RAM/ALARMES/ETATS/ALR_GB_OVF");
        publierAlarme(Alarme::DebordementPetit, "RAM/ALARMES/ETATS/ALR_PB_OVF");
        publierAlarme(Alarme::NiveauMaxGros, "RAM/ALARMES/ETATS/ALR_GB_NIV_MAX");
        publierAlarme(Alarme::NiveauMaxPetit, "RAM/ALARMES/ETATS/ALR_PB_NIV_MAX");
        publierAlarme(Alarme::ConnexionBalance, "RAM/ALARMES/ETATS/ALR_CNX_BAL");
        publierAlarme(Alarme::ConnexionPowerMeter, "RAM/ALARMES/ETATS/ALR_CNX_POW");
        mqtt.envoyer("RAM/PANNEAU/ETATS/TemperatureActive", resultat.temperatureActive ? "ON" : "OFF");
        mqtt.envoyer("RAM/PANNEAU/ETATS/Mode", resultat.automatique ? "AUTO" : "MANUEL");
        if (resultat.disponible)
        {
            const PanelReading &lecture = resultat.lecture;
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

            mqtt.envoyer("RAM/PANNEAU/ETATS/Pompe", panneau.isPumpOn() ? "ON" : "OFF");
        }

        std::this_thread::sleep_until(prochainCycle);
    }
}

bool RunMqtt::prendreMisesAJour(MisesAJour &valeurs)
{
    std::lock_guard<std::mutex> verrou(mutexConsignes);
    if (!misesAJourDisponibles)
        return false;

    valeurs = std::move(enAttente);
    enAttente = MisesAJour{};
    misesAJourDisponibles = false;
    return true;
}

bool RunMqtt::ValeurRecue::lireConsigne(double &valeur) const
{
    if (!recue)
        return false;

    const char *texte = contenu.c_str();
    char *fin = nullptr;
    const double nombre = std::strtod(texte, &fin);
    if (fin == texte || fin != texte + contenu.size() ||
        !std::isfinite(nombre) || nombre < 0.0 || nombre > 100.0)
        return false;

    valeur = nombre;
    return true;
}
