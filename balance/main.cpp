#include "Keyboard.h"
#include "Mqtt.h"
#include "SerialPort.h"
#include <chrono>


#include <cstdio>
#include <sstream>

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage : %s /dev/ttyUSB0\n", argv[0]);
        return 1;
    }

    const int debit = 9600;
    const char parite = 'N';

    if (!port.open(argv[1], debit, parite,7))
    {
        std::fprintf(stderr, "Erreur série sur %s : %s\n",
                     argv[1], port.lastError());
        return 1;
    }

    Mqtt mqtt;

    if (!mqtt.connecter("172.17.15.112", 1883))
    {
        printf("Erreur MQTT : connexion impossible\n");
        return 1;
    }

    if (!mqtt.abonner("RAM/ALARMES/ACK/+"))
    {
        std::fprintf(stderr, "Abonnement aux acquittements d’alarme impossible\n");
    }

    PowerMeter compte(port);
    Keyboard clavier;
    int perdu = 0;
    bool active = true;
    bool panneSimulee = false;

    std::puts("P : simuler ou arrêter la panne Modbus");
    while (true)
    {
        

    if (balance.lireRegistre(trame))
    {
        for (int i = 0; i < 18; i++)
        {
            printf("%02X ", trame[i]);
        }

        printf("\n");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    }
}
