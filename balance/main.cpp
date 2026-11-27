#include "Keyboard.h"
#include "Mqtt.h"
#include "SerialPort.h"
#include <chrono>
#include "balance.h"

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
    const char parite = 'E';

    if (!port.open(argv[1], debit, parite, 7))
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

    Keyboard clavier;
    Balance balance(port);
    int perdu = 0;
    bool active = true;
    bool panneSimulee = false;

    std::array<unsigned char, 18> trame{};
    std::array<unsigned char, 64> tampon{};
    while (true)
    {

        if (balance.lireRegistre(trame))
        {
            for (int i = 0; i < 18; i++)
            {
                printf("%02X ", trame[i]);
            }

            printf("\n");

            if (trame[0] == 0x02)
            {
                printf("OK");
                printf("\n");
            }
            unsigned char etatA = trame[1];
            unsigned char etatB = trame[2];
            unsigned char etatC = trame[3];

            // Mot A
            int decimal = etatA & 0b00000111;
            int increment = (etatA & 0b00011000) >> 3;

            // Mot B
            bool net = etatB & 0b00000001;
            bool negatif = etatB & 0b00000010;
            bool surcapacite = etatB & 0b00000100;
            bool mouvement = etatB & 0b00001000;
            bool kilogrammes = etatB & 0b00010000;

            // Mot C
            bool impression = etatC & 0b00001000;
            printf("Decimal : %d\n"
                   "Increment : %d\n"
                   "Net : %d\n"
                   "Negatif : %d\n"
                   "Surcapacite : %d\n"
                   "Mouvement : %d\n"
                   "Kilogrammes : %d\n"
                   "Impression : %d\n",
                   decimal,
                   increment,
                   net,
                   negatif,
                   surcapacite,
                   mouvement,
                   kilogrammes,
                   impression);

            double poids = 0;
            double tare = 0;
            double facteur = 1;

            // Conversion du poids ASCII
            for (int i = 4; i <= 9; i++)
            {
                if (trame[i] >= '0' && trame[i] <= '9')
                {
                    poids = poids * 10 + (trame[i] - '0');
                }
            }

            // Conversion de la tare ASCII
            for (int i = 10; i <= 15; i++)
            {
                if (trame[i] >= '0' && trame[i] <= '9')
                {
                    tare = tare * 10 + (trame[i] - '0');
                }
            }

            // Position de la virgule
            switch (decimal)
            {
            case 1:
                facteur = 10.0;
                break;
            case 2:
                facteur = 1.0;
                break;
            case 3:
                facteur = 0.1;
                break;
            case 4:
                facteur = 0.01;
                break;
            case 5:
                facteur = 0.001;
                break;
            default:
                continue;
            }

            // Application des décimales
            poids *= facteur;
            tare *= facteur;

            // Application du signe
            if (negatif)
            {
                poids = -poids;
            }

            // Affichage
            printf("Poids : %.3f %s\n",
                   poids, kilogrammes ? "kg" : "lb");

            printf("Tare : %.3f %s\n",
                   tare, kilogrammes ? "kg" : "lb");
                   
        }
        const char touche = clavier.kbhit() ? clavier.getch() : '\0';
        if (touche == 'p' || touche == 'P')
        {
            port.write("P", 1);
        }
        else if(touche == 'c' || touche == 'C')
        {
            port.write("C", 1);
        }
        else if(touche == 't' || touche == 'T')
        {
            port.write("T", 1);
        }
        else if(touche == 'z' || touche == 'Z')
        {
            port.write("Z", 1);
        }
        else if(touche == 's' || touche == 'S')
        {
            port.write("S", 1);
        }
        else if(touche == 'u' || touche == 'U')
        {
            port.write("U", 1);
        }
        else if(touche == 'x' || touche == 'X')
        {
            port.write("X", 1);
        }
        //
        //
        //
        //
        //
        // std::this_thread::sleep_for(std::chrono::milliseconds(200));
        /*int n = port.readBytes(tampon.data(), tampon.size(), 1000);

      if (n < 0)
      {
          printf("Erreur : %s\n", port.lastError());
          break;
      }

      if (n == 0)
      {
          printf("Aucune donnee recue\n");
          continue;
      }

      printf("Recu %d octets : ", n);

      for (int i = 0; i < n; i++)
      {
          printf("%02X ", tampon[i]);
      }

      printf("\n");
      fflush(stdout);*/
    }
}
