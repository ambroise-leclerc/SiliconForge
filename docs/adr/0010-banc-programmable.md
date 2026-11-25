# ADR 0010 — Décrire, piloter et rejouer un banc embarqué

- Statut : proposée
- Date : 2026-11-18

## Contexte et état de l’art

Wokwi exécute des scénarios qui actionnent un bouton et attendent une réponse série. Son format est annoncé en alpha. Renode expose des testeurs de périphériques, notamment pour l’UART. Ces précédents motivent des assertions sur les sorties observables du firmware et un scénario conservé avec le projet. [Scénarios Wokwi](https://docs.wokwi.com/wokwi-ci/automation-scenarios) · [API de test Renode](https://renode.readthedocs.io/en/latest/basic/renode-testing-api.html)

Sur matériel, labgrid associe ressources et pilotes pour commander notamment alimentation et reset. Twister sélectionne les cartes selon les montages requis par un test. La disponibilité du câblage fait donc partie des conditions d’exécution. [labgrid 25.0](https://labgrid.readthedocs.io/en/v25.0/index.html) · [Twister](https://docs.zephyrproject.org/latest/develop/test/twister.html)

## Décision

Le circuit JSON décrit les connexions selon l’[ADR 0005](0005-composants-et-circuits.md). Un scénario versionné séparé référence le circuit et le firmware, fixe l’état initial, puis ordonne stimuli, attentes bornées et assertions. L’interface graphique et la commande sans interface graphique appellent le même moteur. Un essai conserve les empreintes des entrées, la version du moteur, les mesures avec unités, la trace et le verdict. La commande distingue par son code de sortie une assertion manquée d’un banc incompatible.

| Ressource | Commandes et observations attendues |
| --- | --- |
| Cible et câblage | Valider les connexions, charger le HEX ou l’ELF, réinitialiser et avancer jusqu’à une condition ou une échéance. |
| Alimentation pilotable | Activer une voie, régler tension et limite de courant, relever tension et courant obtenus. |
| GBF et générateur d’ondes | Programmer sinus, carré ou triangle par fréquence, amplitude et décalage. Pour une onde arbitraire, fournir les échantillons et leur période. |
| Charge simulée | Connecter une résistance paramétrable et observer le courant et la tension au nœud pendant un changement de charge. |
| Composant externe | Modéliser le protocole et l’état observable. Pour un écran, reconstruire les pixels écrits par le firmware et comparer le tampon à une image attendue. |

Le premier incrément utilisera une entrée commutée et la capture PWM existante. Les modèles d’alimentation, de charge et d’écran seront ajoutés avec des essais de référence propres. Un écran SPI ou I²C dépend d’abord du modèle de bus correspondant. Pour chaque générateur, le modèle publiera sa résolution temporelle et ses bornes électriques. Une demande hors capacité doit échouer avant l’essai. Les stimuli simulés sont planifiés sur l’horloge du MCU et les mesures utilisent les événements de l’[ADR 0007](0007-instruments.md).

## Alternatives et conséquences

Automatiser des clics rendrait les essais dépendants du rafraîchissement de l’interface. Un moteur analogique général dépasserait les hypothèses du modèle résistif actuel. La limitation de courant et la réponse à une charge devront être qualifiées avant d’annoncer ces capacités.

Un futur adaptateur physique décrira les connexions préinstallées, le programmateur et les voies disponibles. Tout recâblage exigera un opérateur ou une matrice de commutation déclarée. L’adaptateur devra réserver les ressources, vérifier les limites du montage avant activation et ramener les sorties à leur état de repos après interruption. Les mesures physiques porteront une tolérance et leur propre base de temps. La qualification initiale concerne le simulateur.

## Mise à jour du 2026-11-20 (#80)

La commande sans fenêtre est devenue `siliconforge bench`, dans l’exécutable de l’application, au lieu d’un
exécutable séparé : `siliconforge` tourne déjà sans fenêtre, et la CI le construit partout. Le moteur, les preuves et
les codes de sortie ne changent pas. Lancée dans le terminal de la fenêtre, la commande confie l’essai à la fenêtre
ouverte ([ADR 0011](0011-agent-mcp.md)), qui l’affiche et charge le circuit et le firmware testés dans l’émulation
visible : l’essai à la main de #61 avait montré qu’un firmware vérifié en ligne de commande restait invisible.

## Mise à jour du 2026-11-21 (#63)

Le GBF et le générateur d’ondes sont le composant `signal-generator`. Les instants des échantillons sont planifiés sur
l’horloge du MCU par `Mcu::schedule`, que le circuit utilise (ADR 0002) : une réinitialisation du MCU efface les
appels en attente, le circuit les replanifie. Le modèle publie ses bornes électriques (±20 V), sa résolution
temporelle (un cycle ; 64 pas par période d’un sinus ou d’un triangle) et sa fréquence maximale à l’horloge de la cible ;
une demande hors capacité est refusée avant l’essai, y compris la suite d’un scénario, rejouée dans l’ordre. Le modèle
reste résistif : la voie est une source idéale qui fournit et absorbe du courant, sans impédance de sortie, et une
charge au-delà de son courant nominal est signalée comme une surcharge.

## Mise à jour du 2026-11-21 (#64)

Les écrans de référence sont ceux que la personne utilise : le module KMRTM28028 (ILI9341, 240 × 320, SPI 4 fils) et
le shield MCUFRIEND 3,5" (ILI9481, 320 × 480, bus parallèle 8 bits). Le second n’est pas SPI ni I²C : le modèle de
bus du composant a donc deux façades, SPI et 8080, sur un contrôleur commun qui suit les commandes qui comptent pour
les pixels (fenêtre, écriture mémoire, orientation, format, veille, inversion, resets). Le MCU gagne un périphérique SPI
maître (`avr.spi`) dont chaque front est daté au cycle ; le composant lit les niveaux des nets à chaque changement de
broche, sans toucher au modèle résistif. Les transactions du bus (commande, cadence, octets, somme) se vérifient
avec un firmware connu avant de qualifier l’écran ; l’étape `screen` d’un scénario compare ensuite le tampon reconstruit à
une image attendue. Le bus I²C n’est pas modélisé : aucun écran de référence ne l’utilise.
