#pragma once

#include <mosquitto.h>
#include <string>

class Mqtt {
public:
    Mqtt();
    ~Mqtt();

    bool connecter(const char* ip, int port = 1883);

    bool envoyer(const std::string& topic,
                 const std::string& message);

    bool envoyer(const std::string& topic,
                 double valeur);

    void deconnecter();

    Mqtt(const Mqtt&) = delete;
    Mqtt& operator=(const Mqtt&) = delete;

private:
    struct mosquitto* client = nullptr;
    bool actif = false;
    bool initialise = false;
};