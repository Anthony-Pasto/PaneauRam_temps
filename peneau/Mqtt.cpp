#include "Mqtt.h"
#include <iostream>
#include <cmath>

Mqtt::Mqtt()
{
    initialise = mosquitto_lib_init() == MOSQ_ERR_SUCCESS;

    if (initialise) {
        client = mosquitto_new(nullptr, true, nullptr);
    }
}

Mqtt::~Mqtt()
{
    deconnecter();

    if (client)
        mosquitto_destroy(client);

    if (initialise)
        mosquitto_lib_cleanup();
}

bool Mqtt::connecter(const char* ip, int port)
{
    if (!client)
        return false;

    if (actif)
        return true;

    int rc = mosquitto_connect(client, ip, port, 60);

    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << mosquitto_strerror(rc) << std::endl;
        return false;
    }

    rc = mosquitto_loop_start(client);

    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_disconnect(client);
        return false;
    }

    actif = true;
    return true;
}

bool Mqtt::envoyer(const std::string& topic,
                   const std::string& message)
{
    if (!actif)
        return false;

    int rc = mosquitto_publish(
        client,
        nullptr,
        topic.c_str(),
        message.size(),
        message.c_str(),
        0,
        false
    );

    return rc == MOSQ_ERR_SUCCESS;
}

bool Mqtt::envoyer(const std::string& topic,
                   double valeur)
{
    long long entier = std::llround(valeur);
    return envoyer(topic, std::to_string(entier));
}

void Mqtt::deconnecter()
{
    if (actif) {
        mosquitto_disconnect(client);
        mosquitto_loop_stop(client, false);
        actif = false;
    }
}