# L’agent dans la fenêtre : le serveur MCP

Le client lancé dans l’onglet **Terminal** (Claude Code, Codex, OpenCode) reçoit les outils `siliconforge` : il charge
le firmware qu’il vient de compiler dans l’émulation affichée, appuie sur un bouton du breadboard, mesure une broche ou
lance un scénario, et la personne voit chacune de ces actions dans la fenêtre. La décision est décrite dans
l’[ADR 0011](adr/0011-agent-mcp.md).

## Ce que voit la personne

- La **carte** et les **instruments** montrent l’effet des actions : firmware chargé, LED allumée, PWM capturée.
- Le journal **Agent**, en tête de l’onglet **Bench**, liste chaque appel, du plus récent au plus ancien : heure,
  outil, arguments et résultat (en rouge pour un refus). Quand l’onglet Bench n’est pas affiché, le nombre d’appels
  non vus s’affiche sur l’onglet.
- Un essai lancé par l’agent (`bench_run`) s’ouvre dans le panneau **Bench**, comme un essai lancé par la personne.
- **Let the agent drive** : décoché, les outils qui modifient l’émulation sont refusés et l’agent reçoit un message
  qui l’explique. Les lectures (`status`, `read_pins`, `measure`, `serial_read`, `bench_*`) restent permises.

## Les outils

| Outil | Arguments | Effet |
| --- | --- | --- |
| `status` | | MCU, horloge, temps simulé, marche ou pause, firmware, composants et leurs états (`settable` : modifiable par `set_component`) |
| `load_firmware` | `path`, `run` (vrai par défaut) | Charge un fichier Intel HEX ou ELF du projet (chemin relatif), depuis le reset, puis le lance |
| `run`, `pause`, `reset` | | Comme les boutons de la fenêtre ; `reset` relance la capture et la sortie série |
| `set_component` | `component`, `state`, `value` | `{"component": "button-d2", "state": "pressed", "value": 1}` maintient le bouton, `0` le relâche ; un potentiomètre va de 0 à 1 |
| `set_voltage` | `pin`, `volts` | Applique une tension par le potentiomètre qui alimente la broche, sinon par une sonde |
| `serial_send` | `text` | Envoie du texte à l’USART0 |
| `serial_read` | `from` (0 par défaut) | Texte émis par l’USART0 depuis le reset ; `next` indique où reprendre |
| `read_pins` | | Niveau (`low`, `high`, `floating`), direction et tension de chaque broche |
| `measure` | `pin`, `quantity`, `window` (s, 0,001 par défaut, 10 au plus) | Rapport cyclique (%), fréquence (Hz) ou période (s), calculés sur les fronts de la capture, datés au cycle près, avec les règles du [banc](BANC.md#mesures). En marche, la fenêtre commence à l’appel et la réponse arrive quand elle est écoulée ; en pause, c’est la dernière fenêtre de la capture |
| `bench_list` | | Scénarios de `scenarios/` et essais de `.siliconforge/runs/` |
| `bench_run` | `scenario` | Lance un scénario avec son propre MCU, comme `siliconforge-bench`, sur le fil du panneau Bench ; renvoie le verdict et les mesures |
| `bench_open` | `run` | Le rapport d’un essai |

Une erreur de l’agent (argument manquant, composant inconnu, chemin hors du projet, fenêtre trop longue) lui revient
comme résultat de l’appel (`isError`), avec un message qui dit quoi corriger.

## Le serveur

- Adresse : `http://<adresse d’écoute>:<port>/mcp/`, affichée au démarrage. Le serveur n’existe que si l’interface
  écoute sur une adresse de bouclage (`127.x.y.z` ou `::1`).
- Transport : Streamable HTTP de MCP, révisions 2025-06-18, 2025-03-26 et 2024-11-05. Les requêtes sont en POST et
  les réponses en `application/json`, sans flux d’événements ; une notification reçoit `202 Accepted`.
- Authentification : `Authorization: Bearer <jeton>`. Sans le bon jeton, la réponse est 401. Une origine qui n’est pas
  une adresse de bouclage reçoit 403, et GET reçoit 405.
- Jeton : 128 bits tirés au hasard à chaque lancement. Pour un jeton fixe, définir `SILICONFORGE_MCP_TOKEN` (16
  caractères au moins) avant de lancer la fenêtre.

Chaque session du terminal reçoit `SILICONFORGE_MCP_URL` et `SILICONFORGE_MCP_TOKEN`. Les clients sont configurés à
leur lancement, sans rien écrire dans leur configuration :

| Client | Configuration passée | Vérifié (2026-11-19, Linux) |
| --- | --- | --- |
| Claude Code | `--mcp-config` (en-tête `Bearer ${SILICONFORGE_MCP_TOKEN}`, développé par le client) | 2.1.329 : « Successfully connected » |
| Codex | `-c mcp_servers.siliconforge.url=…`, `-c mcp_servers.siliconforge.bearer_token_env_var=…` | 0.170.0 : serveur listé, actif, jeton porteur |
| OpenCode | `OPENCODE_CONFIG_CONTENT` (en-tête `Bearer {env:SILICONFORGE_MCP_TOKEN}`) | 1.18.14 : `opencode mcp list` « connected » |
| Mistral Vibe | rien au lancement : il ne lit ses serveurs que dans `config.toml` | 3.1.2 : configuration ci-dessous conforme au schéma de `vibe config schema` ; connexion non vérifiée |

Pour Mistral Vibe, fixer le jeton (`export SILICONFORGE_MCP_TOKEN=…`) et le port (`--port 9002`) de la fenêtre,
puis ajouter à `~/.vibe/config.toml` :

```toml
[[mcp_servers]]
name = "siliconforge"
transport = "streamable-http"
url = "http://127.0.0.1:9002/mcp/"
auth = { type = "static", api_key_env = "SILICONFORGE_MCP_TOKEN", api_key_format = "Bearer {token}" }
```

Le client demande l’autorisation d’utiliser chaque outil selon ses propres règles : en mode par défaut, Claude Code
pose la question à la première utilisation d’un outil `siliconforge`.

## Qualification

- `ctest -R siliconforge_bridge_tests` (cas `[mcp]`) : cycle de vie MCP et négociation de version, liste des outils,
  résultats et erreurs, erreurs JSON-RPC, notifications et lots, interrupteur, journal, route HTTP (jeton, Origin,
  méthode), configuration de chaque client.
- `ctest -R UI-Agent` (fenêtre native, Linux) : le shell du terminal, dans le rôle du client, appelle les outils avec
  `curl`. Le test vérifie le firmware chargé et lancé, la LED D13 allumée par le bouton, la mesure de 75 %, la pastille
  de l’onglet, l’interrupteur qui refuse un `reset` et laisse lire, l’essai ouvert dans le panneau Bench et le refus
  d’un mauvais jeton.
