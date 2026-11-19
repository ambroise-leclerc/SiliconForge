# La boucle de correction PWM, de la consigne à la mesure reproductible

L’[ADR 0008](adr/0008-agentic-foundry.md) évalue l’Agentic Foundry sur un même exercice, avant et après intégration :
partir d’une consigne de rapport cyclique, faire modifier le firmware, compiler avec la chaîne du projet, charger le
HEX, mesurer la PWM sur la broche, corriger, et obtenir une mesure reproductible. On compte les interventions
manuelles et le délai jusqu’à cette mesure (#61). Ce document décrit l’exercice, la façon de le rejouer et les mesures
relevées.

## L’exercice

Le firmware [`pwm_setpoint.c`](../firmware/src/pwm_setpoint.c), sur le circuit de démonstration, règle la PWM de D6
(Timer0, Fast PWM, 62,5 kHz) à 25 % bouton D2 relâché et à 75 % bouton enfoncé.

**Consigne :** bouton relâché, D6 est éteinte (0 %) ; bouton enfoncé, rien ne change.

La consigne devient l’attendu du scénario [`tests/foundry/pwm-setpoint.json`](../tests/foundry/pwm-setpoint.json) :
rapport cyclique entre 0 et 0,1 % relâché, 75 ± 0,5 % enfoncé, 62,5 kHz ± 100 Hz enfoncé. Avant toute modification,
le banc constate l’écart avec le firmware actuel (25 % relâché).

La modification évidente, `SETPOINT_RELEASED_PERCENT 0` ([`1-setpoint-off.patch`](../tests/foundry/1-setpoint-off.patch)),
ne suffit pas : en Fast PWM non inversée, `OCR0A = 0` laisse la sortie à l’état haut pendant un pas du timer à chaque
période : une impulsion de 62,5 ns tous les 16 µs, soit 0,390625 %. Le banc mesure cet écart, la même valeur que sur
le matériel, et conserve la trace où l’impulsion se voit. La correction ([`2-disconnect-oc0a.patch`](../tests/foundry/2-disconnect-oc0a.patch))
déconnecte OC0A pour la consigne 0 % et laisse D6 à l’état bas par PORTD.

```console
$ git apply tests/foundry/1-setpoint-off.patch && make -s -C firmware prebuilt
$ siliconforge-bench run scenarios/pwm-setpoint.json; echo $?
PWM setpoint on D6: fail
  [failed]   1 measure  Released setpoint: off — duty-released = 0.390625 %
  ...
  FAIL duty-released dutyCycle on D6: 0.390625 % (expected 0 to 0.1 %)
  ok   duty-held dutyCycle on D6: 75 % (expected 74.5 to 75.5 %)
  ok   frequency frequency on D6: 62500 Hz (expected 62400 to 62600 Hz)
  FAIL duty-released-again dutyCycle on D6: 0.390625 % (expected 0 to 0.1 %)
1
$ git apply tests/foundry/2-disconnect-oc0a.patch && make -s -C firmware prebuilt
$ siliconforge-bench run scenarios/pwm-setpoint.json; echo $?
PWM setpoint on D6: pass
0
```

La chaîne du projet est `make -C firmware prebuilt` : elle produit le HEX que le scénario charge, et le rapport
nomme son empreinte SHA-256. Deux essais sur les mêmes entrées donnent un rapport et une trace identiques à l’octet
près : c’est la mesure reproductible.

## Rejouer l’exercice

Deux tests rejouent l’exercice dans un projet refait à chaque exécution par
[`FoundryProject.cmake`](../tests/FoundryProject.cmake) : sources et Makefile du firmware, circuit, consigne, dans un
dépôt Git à part. Ils exigent avr-gcc, make et Git.

| Test | Parcours |
| --- | --- |
| `ctest -R FoundryCommandLine` | Le terminal seul. Il constate l’écart de 25 % avant modification. La première modification compilée donne le code de sortie 1 avec 0,390625 % dans les deux phases relâchées, et la mesure enfoncée reste conforme. Il vérifie aussi que le HEX chargé est celui qui vient d’être compilé, que `git diff` montre le changement et que la trace existe. La correction donne le code de sortie 0, et un second essai produit le même rapport et la même trace. |
| `ctest -R UI-Foundry` | La fenêtre native (Linux), avec le shell du terminal intégré dans le rôle de l’agent. L’agent applique sa modification, compile et lance le banc. La personne retrouve le changement en diff dans l’éditeur. Elle ouvre ensuite l’essai échoué dans le panneau **Bench** (consigne, mesure, verdict, trace de D6). L’agent corrige. La personne relance enfin le scénario depuis la fenêtre et obtient un rapport identique à celui de l’agent. Le délai est écrit dans `.siliconforge/foundry-delay.txt` du projet de test. |

`UI-FoundryClient` joue l’exercice avec un vrai client dans le terminal de la fenêtre. Il est facultatif
(`cmake -DSILICONFORGE_FOUNDRY_CLIENT=ON`), car il consomme le quota du compte de l’utilisateur et demande un opérateur.
Le client reçoit la consigne et travaille seul. Chacune de ses questions (confiance dans le dossier, autorisation de
modifier un fichier ou de lancer une commande) est écrite dans `.siliconforge/relay/prompt.txt` du projet de test.
L’opérateur répond dans `answer.txt` : le numéro de la question sur la première ligne, puis les touches à taper (`\r`,
`\x1b` écrits tels quels). `screen.txt` montre l’écran toutes les 3 secondes. Une fois le verdict `pass` obtenu, le test
joue la part de la personne : diff dans l’éditeur, essai dans **Bench**, relance identique depuis la fenêtre. Il écrit
ensuite le délai, les questions et les réponses dans `result.txt`. Le test tourne sous Xvfb avec `GDK_BACKEND=x11` et
sans `WAYLAND_DISPLAY` : il n’ouvre rien sur le bureau.

### À la main, avec un client

1. Préparer un projet neuf (le refaire avant chaque essai) :
   ```bash
   cmake -DSOURCE=$PWD -DPROJECT=$HOME/foundry-exercise -P tests/FoundryProject.cmake
   ```
2. Ouvrir la fenêtre dans ce projet, avec `siliconforge-bench` dans le `PATH` du client :
   ```bash
   cd ~/foundry-exercise
   PATH=~/devel/SiliconForge/build/apps/siliconforge-bench:$PATH ~/devel/SiliconForge/build/apps/siliconforge/siliconforge
   ```
3. Dans le panneau **Terminal**, choisir le client et l’ouvrir. Pour que chaque autorisation passe par la personne, le
   client doit demander avant d’agir : pour Claude Code, mode par défaut et non *auto mode* (Maj+Tab change de mode).
4. Démarrer le chronomètre et donner la consigne :
   > Bouton D2 relâché, la PWM de D6 doit être à 0 % ; bouton enfoncé, rien ne change. Le scénario
   > scenarios/pwm-setpoint.json décrit déjà cette consigne. Modifie le firmware firmware/src/pwm_setpoint.c, compile avec
   > `make -C firmware prebuilt` et vérifie avec `siliconforge-bench run scenarios/pwm-setpoint.json` jusqu’à un verdict
   > pass. Ne modifie pas le scénario.
5. Répondre aux questions du client, en comptant chaque réponse comme une intervention. Noter chaque verdict du banc
   affiché dans le terminal : un `fail` sur `duty-released` est l’écart sur la broche.
6. Une fois le verdict `pass` obtenu, ouvrir **Sources** et relire le diff de `firmware/src/pwm_setpoint.c`. Ouvrir
   ensuite l’essai `pwm-setpoint` dans **Bench**, puis le relancer avec **Run**. Arrêter le chronomètre quand le panneau
   indique *Same report*.
7. Reporter dans le tableau ci-dessous : le client et sa version, le modèle, le mode d’autorisation, le délai, les
   interventions (consigne comprise), les relectures et les verdicts successifs.

## Comptage

Une **intervention manuelle** est une action de la personne sans laquelle la boucle n’avance pas : écrire, compiler,
charger, actionner un stimulus, relever une mesure. Les relectures facultatives (diff, essai ouvert dans la fenêtre)
sont comptées à part. Le **délai** court de la consigne au premier verdict conforme que l’on peut reproduire à l’octet
près.

### Référence manuelle, sans la Foundry

La personne écrit le code dans son éditeur, compile dans un terminal, puis vérifie dans la fenêtre avec la capture
des [instruments](INSTRUMENTS.md). Pour chaque version du firmware, elle :

1. modifie et enregistre `pwm_setpoint.c` ;
2. lance `make -C firmware prebuilt` ;
3. charge `pwm_setpoint.hex` avec le sélecteur de fichier ;
4. lance la simulation et la capture ;
5. mesure le rapport cyclique de D6, bouton relâché, avec les curseurs ;
6. maintient le bouton D2 ;
7. mesure le rapport cyclique et la fréquence de D6 ;
8. relâche le bouton ;
9. mesure de nouveau D6 ;
10. compare les trois mesures à la consigne.

L’exercice compte deux versions du firmware, auxquelles s’ajoute le diagnostic de l’impulsion dans la capture (zoom sur
une période). Il faut donc **21 interventions**. Les curseurs et le bouton sont placés à la main : refaire la mesure
signifie refaire les étapes 3 à 10. Elle n’est pas reproductible à l’octet près ; elle ne l’est qu’à la précision du
placement.

### Avec la Foundry

| Essai | Interventions | Relectures | Délai | Remarques |
| --- | --- | --- | --- | --- |
| Agent (Claude Opus 5.5) dans un terminal, sur une copie du projet, 2026-11-19 | 1 (la consigne) | 0 | 49 s | Trois essais du banc. Le premier montre l’impulsion (0,78 % avant #73, voir les limites). Le deuxième, firmware corrigé, échoue encore : relâchée, D6 n’a plus de front, donc la fréquence ne se mesure plus. L’agent déplace cette mesure dans la phase enfoncée du scénario, et le troisième essai passe, reproduit à l’octet près. Délai mesuré à l’horloge, temps de réponse du modèle compris. |
| `UI-FoundryClient` : Claude Code v2.1.328 (Sonnet 5.5, Claude Pro, *auto mode*) dans le terminal de la fenêtre, Linux x86-64, Xvfb, 2026-11-19 | 2 (confiance dans le dossier, consigne) | 3 (diff, essai, relance) | 21 s | Le client déconnecte OC0A d’emblée et met D6 à l’état bas (« Fast PWM cannot reach 0 % ») : premier verdict conservé `pass`, **aucun écart constaté** sur la broche. En *auto mode*, il modifie et lance `make` et le banc sans rien demander. Le délai va de l’ouverture du client à la relance identique dans la fenêtre. |
| `UI-Foundry`, fenêtre native, Linux x86-64, WebKitGTK, avr-gcc 14.3.0 | 1 (la consigne) | 3 (diff, essai échoué, relance) | 2,1 s | Les modifications de l’agent sont rejouées par le shell. Ce délai mesure la boucle elle-même, sans le temps de réflexion d’un modèle : c’est un plancher. |

Le scénario de l’exercice tient compte de la leçon du premier essai : la mesure de fréquence est dans la phase enfoncée.

## Limites

- **Largeur d’une impulsion d’un cycle.** L’essai de l’agent a eu lieu avant #73. Le cœur datait alors les fronts
  produits par un périphérique à la fin de l’instruction en cours, si bien que l’impulsion apparaissait large de 2
  cycles (0,78 %) ou de 0 cycle (0 %, donc invisible dans `duty-released-again`). Depuis #73, les fronts sont datés au
  cycle où le timer les produit : l’impulsion mesure 0,390625 % dans les deux phases, comme sur le matériel, et les
  tests l’exigent.
- **`make prebuilt` hors du dépôt.** `blink.elf`, compilé avec `-g`, contient le chemin de ses sources : compilé
  depuis une copie du projet, il diffère de celui du dépôt. Les HEX, dont `pwm_setpoint.hex`, n’en dépendent pas.
- Le délai de la référence manuelle n’a pas encore été chronométré, ni l’exercice avec un client qui demande ses
  autorisations. Les deux se mesurent en suivant les procédures ci-dessus.
- Un client qui connaît le piège corrige du premier coup, et l’écart n’apparaît alors pas : `FoundryCommandLine` et
  `UI-Foundry` le démontrent par la première modification enregistrée dans `tests/foundry/1-setpoint-off.patch`.
