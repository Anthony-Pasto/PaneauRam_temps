#include "Keyboard.h"
#include "Mqtt.h"
#include "PowerMeter.h"
#include "SerialPort.h"
#include <chrono>
#include <thread>

#include <cstdio>
#include <sstream>

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage : %s /dev/ttyUSB0\n", argv[0]);
        return 1;
    }

    SerialPort port;

    const int debit = 9600;
    const char parite = 'N';

    if (!port.open(argv[1], debit, parite))
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
        const char touche = clavier.kbhit() ? clavier.getch() : '\0';
        if (touche == 'p' || touche == 'P')
        {
            panneSimulee = !panneSimulee;
            std::puts(panneSimulee ? "Panne Modbus simulée." : "Reprise des lectures Modbus.");
            if (panneSimulee)
                mqtt.envoyer("RAM/ALARMES/ETATS/ALR_CNX_POW", "ON");
        }

        if (!panneSimulee)
        {
            compte.update();
            const auto lecture = compte.getReading();

            if (lecture.connexion && active)
            {
                perdu = 0;
                std::printf("VAN : %.2f V\n", lecture.VAN);
                mqtt.envoyer("RAM/POWERMETER/ETATS/Van", lecture.VAN);
                mqtt.envoyer("RAM/POWERMETER/ETATS/Ia", lecture.IA);
                mqtt.envoyer("RAM/POWERMETER/ETATS/Vbn", lecture.VBN);
                mqtt.envoyer("RAM/POWERMETER/ETATS/Vab", lecture.VAB);
                mqtt.envoyer("RAM/POWERMETER/ETATS/Ib", lecture.IB);
                mqtt.envoyer("RAM/POWERMETER/ETATS/KW", lecture.KW);
                mqtt.envoyer("RAM/POWERMETER/ETATS/FP", lecture.FP * 100.0);
                mqtt.envoyer("RAM/POWERMETER/ETATS/KWh", lecture.KWh);
                mqtt.envoyer("RAM/ALARMES/ETATS/ALR_CNX_POW", "OFF");
            }
            else
            {
                std::puts("Relevé Modbus incomplet ou invalide.");
                ++perdu;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (perdu >= 3)
        {
            perdu = 0;
            active = false;
            mqtt.envoyer("RAM/ALARMES/ETATS/ALR_CNX_POW", "ON");
        }
        if (!active)
        {
            Mqtt::Message message;
            if (mqtt.recevoir(message))
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

                if (domaine == "ALARMES" && categorie == "ACK" && capteur == "ALR_CNX_POW")
                {
                    mqtt.envoyer("RAM/ALARMES/ETATS/ALR_CNX_POW", "OFF");
                    active = true;
                }
            }
        }
    }
}
