# Terminal agentique

L’onglet **Terminal** de la colonne de droite ouvre, dans la fenêtre, le client agentique installé par
l’utilisateur (Claude Code, Codex, OpenCode ou Mistral Vibe) dans le répertoire du projet, ou le shell de
l’utilisateur. La colonne a trois onglets : **Bench** (le banc, le moniteur série et le CPU), **Terminal** et
**Sources** ([éditeur](EDITOR.md)). Choisir Terminal ou Sources élargit la colonne et rétrécit la carte et les
instruments ; les flèches du clavier passent d’un onglet à l’autre, et la fenêtre retient le dernier choisi.

Chaque client reçoit à son lancement le serveur MCP de la fenêtre : ses outils `siliconforge` agissent sur l’émulation
affichée, et chaque appel s’affiche dans le journal de l’onglet Bench ([guide](AGENT-MCP.md),
[ADR 0011](adr/0011-agent-mcp.md)). La décision est décrite dans l’[ADR 0009](adr/0009-poste-agentique.md).

Le client tourne dans un pseudo-terminal natif fourni par WebFront (PTY sous Linux et macOS, ConPTY sous Windows,
[WebFront #613](https://github.com/ambroise-leclerc/WebFront/issues/613)) et s’affiche avec xterm.js. Il garde ses
commandes, ses raccourcis et sa connexion au fournisseur : SiliconForge ne les voit pas.

## Comportement

- **Quels programmes.** L’application nomme les programmes ; la page n’envoie jamais de ligne de commande. Un client
  est proposé s’il est trouvé dans le `PATH` de l’application, sinon il apparaît grisé (« not installed »). Le shell
  est `$SHELL` (`COMSPEC` sous Windows) ; il sert aux commandes du banc comme `siliconforge-bench`.
- **Environnement.** Le client reçoit l’environnement de l’application (`HOME`, `PATH`, sa configuration, ses
  variables de fournisseur) et démarre dans le répertoire de travail de SiliconForge, c’est-à-dire le projet. Les
  variables qui désignent la session d’où l’application a été lancée en sont retirées : `TERM_PROGRAM*`, `VSCODE_*`,
  `CLAUDECODE` et les `CLAUDE_CODE_*` de session (`SSE_PORT`, `SESSION_ID`, `MESSAGING_*`...), `ENABLE_IDE_INTEGRATION`.
  Pour OpenCode : `OPENCODE_EDITOR_SSE_PORT` (il lit aussi `CLAUDE_CODE_SSE_PORT` pour trouver l’éditeur), `ZED_TERM`
  et ses marques `OPENCODE*`. Pour Vibe : `VIBE_SESSION`. Aucune variable de ce genre n’a été trouvée pour Codex ; les
  réglages (`CODEX_HOME`, `MISTRAL_API_KEY`, `VIBE_HOME`, `OPENCODE_CONFIG`...) sont conservés.
  Lancé depuis le terminal de VSCodium où tournait Claude Code, le client embarqué se rattachait sinon à l’IDE de cette
  session et la lui prenait (« switched to another Claude Code session »).
- **Une session par client.** Elle survit au rechargement de la page, au retour à l’onglet **Bench** (**Hide**) et au
  changement de client. **Open** rattache la session qui tourne et rejoue sa sortie récente.
- **Fermeture.** **Close client** termine le client et ses processus au premier plan. Le banc, la simulation et les
  scénarios restent utilisables ; **Restart** ouvre une nouvelle session après la sortie du client.
- **Saisie, taille, interruption.** Les touches vont au client telles quelles ; la taille suit celle du panneau ;
  `Ctrl+C` est transmis au programme au premier plan.
- **Sécurité.** Un terminal est un shell avec les droits de l’utilisateur. WebFront ne l’autorise que sur une adresse
  de bouclage littérale (`127.x.y.z` ou `::1`) et pour les pages de l’origine de l’application ; avec `--address
  0.0.0.0` le panneau est désactivé. Toute page de cette origine accède au même terminal : le port reste local.

## Dans la page

`window.siliconForge.terminal` (voir [`ui/terminal.js`](../ui/terminal.js)) : `clients`, `enabled`, `open(id,
restart)`, `hangUp()`, `show(visible)`, `state` (`{client, running, exitCode}`) et le `Terminal` xterm.js attaché.
Côté C++, `sf.terminal.clients` décrit les programmes et chaque session est le groupe de fonctions
`sf.terminal.<client>.*` de WebFront.

## Qualification

`ctest -R UI-Terminal$` (shell, dans la fenêtre native) vérifie : ouverture dans le répertoire du projet, saisie,
taille, `Ctrl+C`, reprise de la session, fermeture suivie d’un essai de banc, nouvelle session. Il tient lieu de client.

`ctest -R UI-TerminalClients` ouvre chaque client installé sur la machine et l’observe. Il n’exige que ce que le
terminal garantit (le client dessine, la fermeture le termine, le banc fonctionne ensuite) ; le reste dépend de la
version et de la connexion du client et est consigné ici. Il peut afficher une boîte de dialogue au premier lancement
(mise à jour, confiance dans le dossier).

Linux 7.0.0, x86-64, WebKitGTK, 2026-11-19 :

| Client | Version | Dessine | Saisie | Redimensionnement | `Ctrl+C` | Fermeture, banc |
| --- | --- | --- | --- | --- | --- | --- |
| Claude Code | 2.1.328 | oui | oui | oui | quitte au second | oui |
| Codex | 0.170.0 | oui | oui | oui | quitte au second | oui |
| OpenCode | 1.18.14 | oui | oui | oui | quitte au second | oui |
| Mistral Vibe | 3.1.2 | oui | non observée¹ | oui | quitte au premier | oui |

¹ Au premier lancement, Vibe affiche une boîte « Update now / Continue with current version » qui n’affiche pas le
texte tapé ; la sélection se fait aux flèches et à Entrée, qui n’ont pas été essayées.

macOS 27.2.1, arm64, WKWebView, 2026-11-19 :

| Client | Version | Dessine | Saisie | Redimensionnement | `Ctrl+C` | Fermeture, banc |
| --- | --- | --- | --- | --- | --- | --- |
| Claude Code | 2.1.328 | oui | oui | oui | quitte au second | oui |
| Codex | 0.170.0 | oui | oui | oui | quitte au second | oui |
| OpenCode | 1.18.30 | oui | non observée² | oui | quitte au second | oui |
| Mistral Vibe | 1.3.2 | oui | oui | oui | quitte au second | oui |

² OpenCode affichait une boîte « Update Available » qui absorbe le texte tapé, comme Vibe sous Linux¹.

`UI-Terminal` (le shell dans la fenêtre WKWebView) passe : répertoire du projet, saisie, taille, `Ctrl+C`, reprise de
la session, fermeture suivie d’un essai de banc, nouvelle session. Sous macOS seuls `UI-Terminal` et
`UI-TerminalClients` sont raccordés à WKWebView ; la fenêtre exige la session graphique de l’utilisateur.

Windows 11 10.0.22631, x86-64, WebView2 157.0.4341.60 (ConPTY, `cmd.exe`), 2026-11-19 :

| Client | Version | Dessine | Saisie | Redimensionnement | `Ctrl+C` | Fermeture, banc |
| --- | --- | --- | --- | --- | --- | --- |
| Claude Code | 2.1.328 | oui | non observée³ | oui | quitte au second | oui |
| Codex | 0.170.0 | oui | oui | oui | quitte au second | oui |
| OpenCode | — | non installé | — | — | — | — |
| Mistral Vibe | — | non installé | — | — | — | — |

³ Le dossier de la construction n’était pas encore approuvé : Claude Code affichait « Is this a project you created or
one you trust? » (« No, exit » par défaut), qui n’affiche pas le texte tapé. Le dossier n’a pas été approuvé ; le
`Ctrl+C` a donc quitté la boîte de dialogue, pas l’interface principale.

`UI-Terminal` passe sous Windows (9 cas sur 9) avec `cmd.exe` : répertoire du projet, saisie, taille (relevée par
`$Host.UI.RawUI.WindowSize`, la sortie de `mode con` dépendant de la langue de Windows), `Ctrl+C` (une commande `ping`
interrompue), reprise de la session, fermeture suivie d’un essai de banc, nouvelle session. Comme sous macOS, seuls
`UI-Terminal` et `UI-TerminalClients` sont raccordés à WebView2 ; la fenêtre exige la session graphique de
l’utilisateur. La CI Windows les exclut (`-LE ui`) : ils se lancent à la main.

| Plateforme | État |
| --- | --- |
| Linux | qualifié ci-dessus |
| macOS | qualifié ci-dessus |
| Windows (ConPTY) | qualifié ci-dessus ; OpenCode et Vibe n’y sont pas installés, Claude Code attend l’approbation du dossier |

Pour qualifier une plateforme : construire avec le moteur natif, installer les clients, lancer `ctest -R UI-Terminal`
puis ajouter la ligne au tableau. Le test des clients affiche ses observations dans l’échec de l’attente
`qualification <client> {...}` lorsqu’on remplace son `console.log` par une assertion ; `console.log` n’est pas
repris par le rapport Jasmine.
