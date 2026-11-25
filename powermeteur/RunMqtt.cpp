#include "RunMqtt.h"
#include "Mqtt.h"
#include "PowerMeter.h"

#include <chrono>
#include <thread>

RunMqtt::RunMqtt(Mqtt& mqtt, PowerMeter& compteur)
    : Thread("RunMqtt"), mqtt(mqtt), compteur(compteur)
{
}

RunMqtt::~RunMqtt()
{
    stop();
    join();
}

void RunMqtt::task()
{
    // TODO : dans while (enMarche), appeler publier(compteur.getReading())
    // à la cadence choisie. getReading() copie le dernier relevé complet.
    (void)compteur;
}

void RunMqtt::publier(const PowerMeterReading& lecture)
{
    if (!lecture.connexion)
        return;

    // TODO : publier VAN en premier sous RAM/POWERMETER/ETATS/.
    // Faire correspondre les topics et unités à ceux du site.
    // Le compteur fournit KW en kW et KWh en kWh.
    // Utiliser l'envoi de texte pour préserver les décimales : l'envoi
    // d'un double dans Mqtt.cpp arrondit à l'entier.
    // Ajouter les sept autres champs du relevé après le premier essai.
    (void)mqtt;
}
