#include "Mqtt.h"
#include "PowerMeter.h"
#include "RunAcquisition.h"
#include "RunMqtt.h"
#include "SerialPort.h"

#include <cstdio>

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage : %s /dev/ttyUSB0\n", argv[0]);
        return 1;
    }

    SerialPort port;

    // TODO : confirmer le débit et la parité configurés sur l'Elkor.
    const int debit = 9600;
    const char parite = 'N';

    if (!port.open(argv[1], debit, parite))
    {
        std::fprintf(stderr, "Erreur série sur %s : %s\n",
                     argv[1], port.lastError());
        return 1;
    }

    // TODO : pour le premier essai série, tester PowerMeter avant la
    // connexion MQTT. Appeler update() une fois et afficher VAN si
    // getReading().connexion vaut true.
    Mqtt mqtt;

    if (!mqtt.connecter("172.17.15.112", 1883))
    {
        printf("Erreur MQTT : connexion impossible\n");
        return 1;
    }
    // TODO : retirer cet abonnement si le compteur publie seulement.
    if (!mqtt.abonner("RAM/+/CMD/#"))
    {
        std::fprintf(stderr, "Abonnement MQTT impossible\n");
    }
    PowerMeter compte(port);
    std::mutex mutexPowerMeter;
    RunAcquisition valeurRecu(compte);
    RunMqtt runMqttThread(mqtt, compte);

    // TODO : démarrer valeurRecu, puis runMqttThread. Vérifier les retours de
    // start(). Si MQTT refuse de démarrer, arrêter et joindre l'acquisition.
    // TODO : attendre la demande d'arrêt, puis arrêter et joindre les deux
    // threads avant de quitter main, qui possède le port, le compteur et MQTT.
    std::puts("Squelette PowerMeter : commencer par la lecture de VAN.");
    return 0;
}
