# ADR 0008 — Faire de SiliconForge une Agentic Foundry pour l’embarqué

- Statut : acceptée (orientation produit)
- Date : 2026-11-18

## Contexte et état de l’art

Un agent qui sait modifier et compiler un pilote reste bloqué si quelqu’un doit ensuite brancher un bouton, programmer la cible et lire un oscilloscope. Le gain de productivité recherché dépend donc du temps entre une modification et une observation exploitable du firmware.

En novembre 2026, Thoughtworks rapporte utiliser quotidiennement plusieurs agents en terminal. Claude Code propose aussi des intégrations dans les IDE. Le remplacement de l’IDE comme point d’entrée est un choix de SiliconForge pour cette boucle de travail. [Technology Radar, novembre 2026](https://www.thoughtworks.com/content/dam/thoughtworks/documents/radar/2026/11/tr_technology_radar_vol_35_en.pdf) · [Documentation de Claude Code](https://code.claude.com/docs/en/overview)

## Décision

Le projet de firmware devient l’espace de travail commun de l’agent, de la personne qui relit le code et du banc. Depuis cet espace, l’agent doit pouvoir compiler avec la chaîne du projet, charger le binaire, préparer le circuit, appliquer un stimulus, mesurer la réponse puis corriger le code. La personne doit pouvoir reprendre la main et examiner le même essai.

Le premier parcours utilise l’ATmega328P simulé, les fichiers de circuit et la capture existante. La première démonstration partira d’une consigne de rapport cyclique, modifiera le firmware, chargera le HEX et comparera la PWM mesurée sur la broche à la consigne. Un écart devra produire une mesure et une trace consultables même si l’essai échoue.

## Alternatives et conséquences

Une extension d’IDE simplifierait l’édition mais lierait la conduite du banc au cycle de vie d’un éditeur. Une interface de conversation propriétaire imposerait de maintenir un agent supplémentaire. Nous concentrons l’investissement sur le terminal et la revue des sources ([ADR 0009](0009-poste-agentique.md)), ainsi que sur le banc programmable ([ADR 0010](0010-banc-programmable.md)).

Nous évaluerons cette orientation sur le même exercice avant et après intégration, en comptant les interventions manuelles et le délai jusqu’à une mesure reproductible. La vitesse de génération du code seule ne permet pas d’estimer le gain sur banc. L’exercice et ses mesures sont décrits dans [FOUNDRY.md](../FOUNDRY.md).
