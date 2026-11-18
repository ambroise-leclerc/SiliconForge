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
