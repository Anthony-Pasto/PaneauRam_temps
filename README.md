# Panneau RAM

Application C++ pour piloter deux bassins sur Raspberry Pi. Le programme lit les niveaux et les températures, commande les vannes et la pompe, et échange des commandes et des états avec un superviseur MQTT.

## Compiler et lancer

Sur le Raspberry Pi, installe les outils de compilation et la bibliothèque Mosquitto si nécessaire :

```bash
sudo apt install g++ make libmosquitto-dev mosquitto-clients
cd peneau
make -j2
```

Exécute `cd peneau` depuis la racine du dépôt. Compile avec ton compte habituel pour que les fichiers générés restent accessibles lors de la prochaine compilation.

| Installation | Commande à lancer dans `peneau` |
|---|---|
| Panneau physique avec adaptateur sur `/dev/ttyUSB0` | `./panneau /dev/ttyUSB0` |
| Simulateur via le lien `/tmp/ttyRAM` | `./panneau /tmp/ttyRAM 300` |

Pour identifier l’adaptateur du panneau physique, utilise `ls -l /dev/serial/by-id/`. Le deuxième argument est le délai maximal de réponse série, en millisecondes. Sa valeur par défaut est 200 ms.

Pour le simulateur, laisse `socat` tourner dans un autre terminal sur le Pi. Remplace `IP_DU_SIMULATEUR` par l’adresse du PC qui lance le simulateur :

```bash
socat pty,link=/tmp/ttyRAM,raw,echo=0 tcp:IP_DU_SIMULATEUR:5100
```

Le programme se connecte au courtier MQTT configuré dans `peneau/main.cpp` à l’adresse `172.17.15.112:1883` avant d’ouvrir le port série. Lance le programme dans un terminal interactif pour utiliser le clavier.

## Utilisation au clavier

| Touche | Action |
|---|---|
| `A` / `M` | Passer en automatique ou en manuel |
| `G` / `S` / `U` | Sélectionner la consigne de niveau du gros ou du petit bassin, ou la consigne de température du petit bassin. Saisir la valeur puis appuyer sur Entrée. |
| `T` | Activer ou désactiver la régulation de température |
| `P` | Basculer la pompe |
| `1` à `4`, puis `+` / `-` | En manuel, sélectionner une vanne et changer son ouverture par pas de 10 % |
| `O` / `Q` | Commander l’arrêt des sorties ou quitter le programme |

## MQTT

Publie les demandes sur `RAM/PANNEAU/CMD/<commande>` en texte simple, avec **Retain** désactivé.

| Commande | Valeur |
|---|---|
| `Mode` | `AUTO` ou `MANUEL` (`MANUAL` est aussi accepté) |
| `ConsNivGB`, `ConsNivPB` | Consigne de niveau entre 0 et 100 % |
| `ConsTempPB` ou `ConsTmpPB` | Consigne de température du petit bassin entre 0 et 100 °C |
| `TemperatureActive` | `ON` ou `OFF`. La régulation agit en mode automatique. |
| `Pompe` | `ON` / `OFF` ou `1` / `0` |
| `ValveGB`, `ValvePB`, `ValveEC`, `ValveEF` | Ouverture entre 0 et 100 %, en manuel |
| `ValveEEC`, `ValveEEF` | `ON` ou `OFF` pour les électrovannes d’eau chaude et froide, en manuel |

Les commandes sont appliquées lorsqu’une lecture du panneau est disponible et qu’aucune alarme de débordement ne les bloque.

- `RAM/PANNEAU/ETATS/Mode` publie `AUTO` ou `MANUEL`.
- `RAM/PANNEAU/ETATS/TemperatureActive` publie `ON` ou `OFF` selon le réglage, y compris après un changement au clavier.
- `RAM/PANNEAU/ETATS/` publie aussi les niveaux, températures, ouvertures des vannes et états de la pompe et des électrovannes.
- Les contacts de débordement sont publiés sur `RAM/ALARMES/ETATS/ALR_GB_OVF` et `RAM/ALARMES/ETATS/ALR_PB_OVF`.

Pour observer les retours du mode et de la régulation :

```bash
mosquitto_sub -h 172.17.15.112 -v \
  -t 'RAM/PANNEAU/ETATS/Mode' \
  -t 'RAM/PANNEAU/ETATS/TemperatureActive'
```

## Comportement en cas de défaut série

Lorsqu’une lecture ou une commande automatique échoue, `RunPID` commande l’arrêt de la pompe en premier, puis la fermeture des entrées d’eau et des vannes. L’écran indique ensuite si la fermeture a été confirmée. Les commandes normales sont bloquées pendant le défaut. Le programme retente la fermeture si nécessaire, puis reste en manuel après la reprise des lectures.

Sur le panneau physique, une liaison coupée peut empêcher les commandes de fermeture d’arriver. Si la pompe tourne encore et que le bassin risque de déborder, utilise l’arrêt d’urgence du panneau.

## Planification de la PFI

### Unité de contrôle du panneau RAM

| Livrable | Date prévue | État |
|---|---|---|
| Création du dépôt GitHub et rédaction de la planification | 12 novembre 2026 | Planification rédigée |
| Configuration du projet C++ sur Raspberry Pi | 19 novembre 2026 | Makefile en place |
| Établissement de la communication avec le panneau RAM | 19 novembre 2026 | En place, validation physique à compléter |
| Lecture des capteurs de niveau et de température | 19 novembre 2026 | Implémentée |
| Implémentation du contrôle manuel des valves et de la pompe | 19 novembre 2026 | Implémenté |
| Implémentation du PID pour le niveau des deux bassins | 26 novembre 2026 | Implémenté |
| Implémentation des alarmes de débordement et de niveau maximum | 26 novembre 2026 | Contacts de débordement pris en charge |
| Implémentation des systèmes de secours et de sécurité | 26 novembre 2026 | Arrêt sur défaut série implémenté, validation physique à compléter |
| Tests du PID, des alarmes et des systèmes de secours | 26 novembre 2026 | À faire |
| Implémentation du PID de température du petit bassin | 3 décembre 2026 | Implémenté |
| Implémentation de la communication MQTT avec le superviseur | 3 décembre 2026 | Publication et réception implémentées |
| Synchronisation des commandes locales et distantes | 3 décembre 2026 | Implémentée, essais complets à faire |
| Tests complets des modes manuel et automatique | 10 décembre 2026 | À faire |
| Validation du fonctionnement et correction des problèmes | 10 décembre 2026 | À faire |
| Finalisation du plan d'essai et de la documentation | 10 décembre 2026 | À faire |
| Remise finale du dépôt Git et des quatre documents exigés | 11 décembre 2026 | À faire |
