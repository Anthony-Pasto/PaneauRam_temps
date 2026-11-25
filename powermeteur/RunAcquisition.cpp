#include "RunAcquisition.h"
#include "PowerMeter.h"

#include <chrono>
#include <thread>

RunAcquisition::RunAcquisition(PowerMeter& compteur)
    : Thread("RunAcquisition"), compteur(compteur)
{
}

RunAcquisition::~RunAcquisition()
{
    stop();
    join();
}

void RunAcquisition::task()
{
    // TODO : appeler compteur.update() dans while (enMarche), puis attendre
    // le prochain cycle. update() lit déjà les huit registres. Sa cadence
    // doit donc tenir compte du temps série. Seul ce thread appelle update().
    (void)compteur;
}
